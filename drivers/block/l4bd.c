/*
 * Block driver for l4fdx.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <asm/api/macros.h>

#include <asm/generic/l4lib.h>
#include <asm/generic/vcpu.h>

#include <l4/libfdx/fdx-c.h>

#include <l4/sys/ktrace.h>

MODULE_AUTHOR("Adam Lackorzynski <adam@l4re.org");
MODULE_DESCRIPTION("Driver for the L4 FDX interface");
MODULE_LICENSE("GPL");

L4_EXTERNAL_FUNC(l4fdxc_init);
L4_EXTERNAL_FUNC(l4fdxc_ping);
L4_EXTERNAL_FUNC(l4fdxc_obj_size);
L4_EXTERNAL_FUNC(l4fdxc_open);
L4_EXTERNAL_FUNC(l4fdxc_read);
L4_EXTERNAL_FUNC(l4fdxc_write);
L4_EXTERNAL_FUNC(l4fdxc_fstat);
L4_EXTERNAL_FUNC(l4fdxc_close);

enum {
	NR_DEVS = 4,
	POLL_INTERVAL = 500,
	MINORS_PER_DISK = 16,
};

static char fdxcap[16];
module_param_string(fdxcap, fdxcap, sizeof(fdxcap), 0);
MODULE_PARM_DESC(fdxcap, "L4 block driver fdx cap (mandatory to enable driver)");

static int major_num = 0;        /* kernel chooses */
module_param(major_num, int, 0);

#define KERNEL_SECTOR_SIZE 512

/*
 * Our request queue.
 */
static struct request_queue *queue;

struct l4bd_server_conn {
	l4fdxc fdxc;
	void *fdxcobj;
};

/* Currently we can only talk to one server */
static struct l4bd_server_conn srv;

/*
 * The internal representation of our device.
 */
struct l4bd_device {
	unsigned long size; /* Size in Kbytes */
	spinlock_t lock;
	u8 *data;
	struct gendisk *gd;
	struct l4bd_server_conn *srv;
	int fdxc_fid;
	int open_flags;
	bool sync_probe;
	char path[60];
};

static struct l4bd_device device[NR_DEVS];
static int nr_devs;

/*
 * Handle an I/O request.
 */
static int transfer_sync(struct l4bd_device *dev, unsigned long long start,
                         unsigned long size, char *buffer, int write)
{
	int r;

	if (((start + size) >> 10) > dev->size) {
		pr_notice("l4bd: access beyond end of device (%lld, %ld)\n",
		          start + size, size);
		return -EIO;
	}

	if (write)
		r = l4fdxc_write(dev->srv->fdxc, dev->fdxc_fid, buffer,
		                 size, start);
	else
		r = l4fdxc_read(dev->srv->fdxc, dev->fdxc_fid, buffer,
		                size, start);

	if (r < 0)
		pr_err("l4bd: request returned with error %d\n", r);

	return r;
}

static void l4bd_request_sync(struct request_queue *q)
{
	struct request *req;

	while ((req = blk_peek_request(q))) {
		struct req_iterator iter;
		struct bio_vec *bvec;
		unsigned long long s;
		int r = 0;

		blk_start_request(req);
		if (req->cmd_type != REQ_TYPE_FS) {
			pr_notice("Skip non-CMD request\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}

		s = blk_rq_pos(req) * KERNEL_SECTOR_SIZE;

		rq_for_each_segment(bvec, req, iter) {
			 r = transfer_sync(req->rq_disk->private_data,
			                   s,
			                   bvec->bv_len,
			                   page_address(bvec->bv_page)
			                     + bvec->bv_offset,
			                   rq_data_dir(req) == WRITE);
			 if (r < 0)
				 break;
			s += bvec->bv_len;
		}
		__blk_end_request_all(req, r < 0 ? -EIO : 0);
	}
}

static int l4bd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct l4bd_device *d = bdev->bd_disk->private_data;
	geo->cylinders = d->size << 5;
	geo->heads     = 4;
	geo->sectors   = 32;
	return 0;
}


/*
 * The device operations structure.
 */
static struct block_device_operations l4bd_ops = {
	.owner = THIS_MODULE,
	.getgeo = l4bd_getgeo,
};

enum init_one_return {
	INIT_OK,
	INIT_ERR_NOT_FOUND,
	INIT_ERR_CREATE_FAILED,
};

static enum init_one_return init_one(int idx)
{
	int ret;
	struct l4fdx_stat_t stbuf;
	struct l4bd_device *d = &device[idx];

	d->srv = &srv;

	d->fdxc_fid = L4XV_FN_l(l4fdxc_open(d->srv->fdxc, d->path,
	                                    d->open_flags, 0));
	if (d->fdxc_fid < 0) {
		pr_err("l4bd: did not find %s (%d)\n", d->path, d->fdxc_fid);
		return INIT_ERR_NOT_FOUND;
	}

	ret = L4XV_FN_i(l4fdxc_fstat(d->srv->fdxc, d->fdxc_fid, &stbuf));
	if (ret) {
		pr_err("l4bd: Could not stat: %d\n", ret);
		goto out1;
	}

	/* l4bd_device.size is unsigned and in KBytes */
	d->size = (stbuf.size + 1023) >> 10;

	spin_lock_init(&d->lock);
	d->data = NULL;

	/* Get a request queue. One would be enough? */
	queue = blk_init_queue(l4bd_request_sync, &d->lock);
	if (queue == NULL)
		goto out1;

	/* gendisk structure. */
	d->gd = alloc_disk(MINORS_PER_DISK);
	if (!d->gd)
		goto out2;
	d->gd->major        = major_num;
	d->gd->first_minor  = MINORS_PER_DISK * idx;
	d->gd->fops         = &l4bd_ops;
	d->gd->private_data = d;
	snprintf(d->gd->disk_name, sizeof(d->gd->disk_name), "l4bd%d", idx);
	d->gd->disk_name[sizeof(d->gd->disk_name) - 1] = 0;
	set_capacity(d->gd, d->size * 2 /* 2 * kb = 512b-sectors */);
	d->gd->queue = queue;
	add_disk(d->gd);

	pr_info("l4bd: Disk %s (%s,%d,%s,%o) size = %lu KB (%lu MB), dev=%d:%d\n",
	        d->gd->disk_name,
	        fdxcap, d->fdxc_fid, d->path, d->open_flags,
	        d->size, d->size >> 10, major_num, d->gd->first_minor);

	return INIT_OK;

out2:
	blk_cleanup_queue(queue);
out1:
	L4XV_FN_v(l4fdxc_close(d->srv->fdxc, d->fdxc_fid));
	return INIT_ERR_CREATE_FAILED;
}

static int do_probe(void *d)
{
	int i = (long)d;

	while (L4XV_FN_i(l4fdxc_ping(fdxcap)))
		msleep(POLL_INTERVAL);

	while (1) {
		enum init_one_return r = init_one(i);

		if (r == INIT_ERR_CREATE_FAILED)
			return -ENOMEM;
		if (r == INIT_OK)
			return 0;

		msleep(POLL_INTERVAL);
	}
}

static int __init l4bd_init(void)
{
	int i, ret, probe_failed = 0;

	if (!fdxcap[0]) {
		pr_err("l4bd: No fdx cap-name (fdxcap) given, not starting.\n");
		return -ENODEV;
	}

	major_num = register_blkdev(major_num, "l4bd");
	if (major_num <= 0) {
		pr_warn("l4bd: unable to get major number\n");
		return -ENOMEM;
	}

	ret = -ENOMEM;
	srv.fdxcobj = kmalloc(l4fdxc_obj_size(), GFP_KERNEL);
	if (!srv.fdxcobj)
		goto out1;

	ret = L4XV_FN_i(l4fdxc_init(fdxcap, &srv.fdxc, srv.fdxcobj));
	if (ret) {
		pr_err("l4bd: Failed to connect to '%s': %d\n", fdxcap, ret);
		goto out2;
	}

	pr_info("l4bd: Found L4fdx server at cap \'%s\'.\n", fdxcap);

	for (i = 0; i < nr_devs; ++i) {
		if (device[i].sync_probe) {
			ret = do_probe((void *)(long)i);
			if (ret < 0)
				probe_failed++;
		} else {
			struct task_struct *k;
			k = kthread_run(do_probe, (void *)(long)i,
			                "l4bd_probe/%d", i);
			if (IS_ERR(k)) {
				ret = PTR_ERR(k);
				probe_failed++;
			}
		}
	}

	if (probe_failed == nr_devs)
		goto out2;

	return 0;

out2:
	kfree(srv.fdxcobj);
out1:
	unregister_blkdev(major_num, "l4bd");

	return ret;
}

static void __exit l4bd_exit(void)
{
	int i;
	for (i = 0; i < nr_devs; ++i) {
		del_gendisk(device[i].gd);
		put_disk(device[i].gd);
	}
	unregister_blkdev(major_num, "l4bd");
	blk_cleanup_queue(queue);
	kfree(srv.fdxcobj);
}

module_init(l4bd_init);
module_exit(l4bd_exit);

static int l4bd_setup(const char *val, struct kernel_param *kp)
{
	const char *p = NULL;
	unsigned l = strlen(val);
	ptrdiff_t path_len = strlen(val);

	if (nr_devs >= NR_DEVS) {
		pr_err("l4bd: Too many block devices specified\n");
		return -ENOMEM;
	}
	if (l > 3 && (p = strstr(val, ",rw"))
	    && (*(p + 3) == ',' || *(p + 3) == 0)) {
		path_len = min(path_len, p - val);
		device[nr_devs].open_flags |= O_RDWR;
	}
	if (l > 10 && (p = strstr(val, ",oflags="))) {
		char *e = 0;
		unsigned flags = simple_strtoul(p + 8, &e, 0);

		if (e > p + 8 && (*e == ',' || *e == 0)) {
			device[nr_devs].open_flags |= flags;
			path_len = min(path_len, p - val);
		}
	}
	if (l > 5 && (p = strstr(val, ",sync"))) {
		device[nr_devs].sync_probe = true;
		path_len = min(path_len, p - val);
	}

	if (path_len > sizeof(device[nr_devs].path) - 1)
		return -ENOMEM;

	strlcpy(device[nr_devs].path, val, path_len + 1);
	nr_devs++;
	return 0;
}

module_param_call(add, l4bd_setup, NULL, NULL, 0200);
MODULE_PARM_DESC(add, "Use one l4bd.add=path[,rw][,oflags=0...][,sync] for each block device to add");
