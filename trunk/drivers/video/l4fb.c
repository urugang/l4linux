/*
 * Framebuffer driver
 *
 * based on vesafb.c
 *
 * Adam Lackorzynski <adam@os.inf.tu-dresden.de>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>

#include <asm/generic/l4lib.h>
#include <l4/sys/err.h>
#include <l4/sys/task.h>
#include <l4/sys/factory.h>
#include <l4/sys/icu.h>
#include <l4/re/c/util/video/goos_fb.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/namespace.h>
#include <l4/re/c/util/cap_alloc.h>
#include <l4/re/c/util/cap.h>
#include <l4/re/c/event_buffer.h>
#include <l4/log/log.h>

#include <asm/l4lxapi/misc.h>

#include <asm/generic/setup.h>
#include <asm/generic/l4fb.h>
#include <asm/generic/cap_alloc.h>
#include <asm/generic/vcpu.h>
#ifdef CONFIG_L4_FB_DRIVER_DBG
#include <asm/generic/stats.h>
#endif

L4_EXTERNAL_FUNC(l4re_util_video_goos_fb_setup_name);
L4_EXTERNAL_FUNC(l4re_util_video_goos_fb_buffer);
L4_EXTERNAL_FUNC(l4re_util_video_goos_fb_goos);
L4_EXTERNAL_FUNC(l4re_util_video_goos_fb_view_info);
L4_EXTERNAL_FUNC(l4re_util_video_goos_fb_destroy);
L4_EXTERNAL_FUNC(l4re_util_video_goos_fb_refresh);
L4_EXTERNAL_FUNC(l4re_event_buffer_consumer_foreach_available_event);
L4_EXTERNAL_FUNC(l4re_event_get);
L4_EXTERNAL_FUNC(l4re_event_buffer_attach);

static l4re_util_video_goos_fb_t goos_fb;
static l4re_video_view_info_t fbi;
static int disable, touchscreen, abs2rel;
static int verbose, singledev;
static const unsigned int unmaps_per_refresh = 1;

static int redraw_pending;

static unsigned int        l4fb_refresh_sleep = HZ / 10;
static int                 l4fb_refresh_enabled = 1;
static struct timer_list   refresh_timer;

static l4_cap_idx_t ev_ds = L4_INVALID_CAP, ev_irq = L4_INVALID_CAP;
static long irqnum;
static l4re_event_buffer_consumer_t ev_buf;
/* Mouse and keyboard are split so that mouse button events are not
 * treated as keyboard events in the Linux console. */
struct input_dev *l4input_dev_key;
struct input_dev *l4input_dev_mouse;

static struct l4fb_unmap_info_struct {
	unsigned int map[L4_MWORD_BITS - 1 - L4_PAGESHIFT];
	unsigned int top;
	unsigned int weight;
	l4_fpage_t *flexpages;
} l4fb_unmap_info;

#ifdef CONFIG_L4_FB_DRIVER_DBG
static struct dentry *debugfs_dir, *debugfs_unmaps, *debugfs_updates;
static unsigned int stats_unmaps, stats_updates;
#endif

/* -- module paramter variables ---------------------------------------- */

static int refreshsleep = -1;

/* -- framebuffer variables/structures --------------------------------- */

static struct fb_var_screeninfo const l4fb_defined = {
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.right_margin	= 32,
	.upper_margin	= 16,
	.lower_margin	= 4,
	.vsync_len	= 4,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static const struct fb_fix_screeninfo l4fb_fix = {
	.id	= "l4fb",
	.type	= FB_TYPE_PACKED_PIXELS,
	.accel	= FB_ACCEL_NONE,
};

static u32 pseudo_palette[17];

/* -- implementations -------------------------------------------------- */

static void vesa_setpalette(int regno, unsigned red, unsigned green,
			    unsigned blue)
{
#if 0
	struct { u_char blue, green, red, pad; } entry;
	int shift = 16 - depth;

	if (pmi_setpal) {
		entry.red   = red   >> shift;
		entry.green = green >> shift;
		entry.blue  = blue  >> shift;
		entry.pad   = 0;
	        __asm__ __volatile__(
                "call *(%%esi)"
                : /* no return value */
                : "a" (0x4f09),         /* EAX */
                  "b" (0),              /* EBX */
                  "c" (1),              /* ECX */
                  "d" (regno),          /* EDX */
                  "D" (&entry),         /* EDI */
                  "S" (&pmi_pal));      /* ESI */
	} else {
		/* without protected mode interface, try VGA registers... */
		outb_p(regno,       dac_reg);
		outb_p(red   >> shift, dac_val);
		outb_p(green >> shift, dac_val);
		outb_p(blue  >> shift, dac_val);
	}
#endif
}

static int l4fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned transp,
			  struct fb_info *info)
{
	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */
	
	if (regno >= info->cmap.len)
		return 1;

	if (info->var.bits_per_pixel == 8)
		vesa_setpalette(regno,red,green,blue);
	else if (regno < 16) {
		switch (info->var.bits_per_pixel) {
		case 16:
			((u32*) (info->pseudo_palette))[regno] =
				((red   >> (16 -   info->var.red.length)) <<   info->var.red.offset) |
				((green >> (16 - info->var.green.length)) << info->var.green.offset) |
				((blue  >> (16 -  info->var.blue.length)) <<  info->var.blue.offset);
			break;
		case 24:
		case 32:
			red   >>= 8;
			green >>= 8;
			blue  >>= 8;
			((u32 *)(info->pseudo_palette))[regno] =
				(red   << info->var.red.offset)   |
				(green << info->var.green.offset) |
				(blue  << info->var.blue.offset);
			break;
		}
	}

	return 0;
}

static int l4fb_pan_display(struct fb_var_screeninfo *var,
                            struct fb_info *info)
{
	return 0;
}

static void (*l4fb_update_rect)(int x, int y, int w, int h);

static l4fb_input_event_hook_function_type l4fb_input_event_hook_function;

void l4fb_input_event_hook_register(l4fb_input_event_hook_function_type f)
{
	l4fb_input_event_hook_function = f;
}
EXPORT_SYMBOL(l4fb_input_event_hook_register);

void l4fb_refresh_status_set(int status)
{
	l4fb_refresh_enabled = status;
}
EXPORT_SYMBOL(l4fb_refresh_status_set);

#if 0
l4_threadid_t l4fb_con_con_id_get(void)
{
	return con_id;
}
EXPORT_SYMBOL(l4fb_con_con_id_get);

l4_threadid_t l4fb_con_vc_id_get(void)
{
	return vc_id;
}
l4dm_dataspace_t l4fb_con_ds_id_get(void)
{
	return fbds;
}      
EXPORT_SYMBOL(l4fb_con_vc_id_get);
#endif

static void l4fb_l4re_update_rect(int x, int y, int w, int h)
{
	L4XV_V(f);
	L4XV_L(f);
	l4re_util_video_goos_fb_refresh(&goos_fb, x, y, w, h);
	L4XV_U(f);
}

static l4_addr_t _fb_addr, _fb_line_length;

static void l4fb_l4re_update_memarea(l4_addr_t base, l4_addr_t size)
{
	int y, h;

	if (size < 0 || base < _fb_addr)
		LOG_printf("l4fb: update: WRONG VALS: sz=%ld base=%lx start=%lx\n",
		           size, base, _fb_addr);

	y = ((base - _fb_addr) / _fb_line_length);
	h = (size / _fb_line_length) + 1;

#ifdef CONFIG_L4_FB_DRIVER_DBG
	++stats_updates;
#endif

	l4fb_l4re_update_rect(0, y, fbi.width, h);
}

// actually we would need to make a copy of the screen to make is possible
// to restore the before-blank contents on unblank events
#if 0
static int l4fb_blank(int blank, struct fb_info *info)
{
	// pretent the screen is off
	if (blank != FB_BLANK_UNBLANK)
		memset((void *)info->fix.smem_start, 0, info->fix.smem_len);
	return 0;
}
#endif

static void l4fb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	cfb_copyarea(info, region);
	if (l4fb_update_rect)
		l4fb_update_rect(region->dx, region->dy, region->width, region->height);
}

static void l4fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
	if (l4fb_update_rect)
		l4fb_update_rect(rect->dx, rect->dy, rect->width, rect->height);
}

static void l4fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	cfb_imageblit(info, image);
	if (l4fb_update_rect)
		l4fb_update_rect(image->dx, image->dy, image->width, image->height);
}

static int l4fb_open(struct fb_info *info, int user)
{
	return 0;
}

static int l4fb_release(struct fb_info *info, int user)
{
	return 0;
}

static int l4fb_mmap(struct fb_info *info,
                     struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long pfn;

	if (offset + size > info->fix.smem_len)
		return -EINVAL;

	pfn = ((unsigned long)info->fix.smem_start + offset) >> PAGE_SHIFT;
	while (size > 0) {
		if (remap_pfn_range(vma, start, pfn, PAGE_SIZE, PAGE_SHARED)) {
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pfn++;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}
	l4_touch_rw((char *)info->fix.smem_start + offset,
	            vma->vm_end - vma->vm_start);

	return 0;
}

static struct fb_ops l4fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open        = l4fb_open,
	.fb_release     = l4fb_release,
	.fb_setcolreg	= l4fb_setcolreg,
	.fb_pan_display	= l4fb_pan_display,
	.fb_fillrect	= l4fb_fillrect,
	.fb_copyarea	= l4fb_copyarea,
	.fb_imageblit	= l4fb_imageblit,
	.fb_mmap	= l4fb_mmap,
	//.fb_blank	= l4fb_blank,
};

/* ============================================ */

static unsigned last_rel_x, last_rel_y;

L4_CV static void input_event_put(l4re_event_t *event)
{
	struct input_event *e = (struct input_event *)event;
	enum {
		EV_CON = 0x10, EV_CON_REDRAW = 1,
	};

	/* Prevent input events before system is up, see comment in
	 * DOpE input function for more. */
	if (system_state != SYSTEM_RUNNING) {
		/* Serve pending redraw requests later */
		if (e->type == EV_CON && e->code == EV_CON_REDRAW)
			redraw_pending = 1;
		return;
	}

	if (l4fb_input_event_hook_function)
		if (l4fb_input_event_hook_function(e->type, e->code))
			return;

	/* console sent redraw event -- update whole screen */
	if (e->type == EV_CON && e->code == EV_CON_REDRAW) {
		l4fb_l4re_update_rect(0, 0, fbi.width, fbi.height);
		return;
	}

	if (abs2rel && e->type == EV_ABS) {
		unsigned tmp;
		// x and y are enough?
		if (e->code == ABS_X) {
			e->type = EV_REL;
			e->code = REL_X;
			tmp = e->value;
			e->value = e->value - last_rel_x;
			last_rel_x = tmp;
		} else if (e->code == ABS_Y) {
			e->type = EV_REL;
			e->code = REL_Y;
			tmp = e->value;
			e->value = e->value - last_rel_y;
			last_rel_y = tmp;
		}
	}

	if (touchscreen && e->type == EV_KEY && e->code == BTN_LEFT)
		e->code = BTN_TOUCH;

	/* The l4input library is based on Linux-2.6, so we're lucky here */
	if (e->type == EV_KEY && e->code < BTN_MISC) {
		input_event(l4input_dev_key, e->type, e->code, e->value);
		input_sync(l4input_dev_key);
	} else {
		input_event(l4input_dev_mouse, e->type, e->code, e->value);
		input_sync(l4input_dev_mouse);
	}
}


static irqreturn_t event_interrupt(int irq, void *data)
{
	l4re_event_buffer_consumer_foreach_available_event(&ev_buf, input_event_put);
	return IRQ_HANDLED;
}

static int l4fb_input_setup(void)
{
	unsigned int i;
	int err;

	if ((irqnum = l4x_register_irq(ev_irq)) < 0)
		return -ENOMEM;

	if ((err = request_irq(irqnum, event_interrupt,
	                       IRQF_SAMPLE_RANDOM, "L4fbev", NULL))) {
		printk("%s: request_irq failed: %d\n", __func__, err);
		return err;
	}

	l4input_dev_key   = input_allocate_device();
	if (singledev)
		l4input_dev_mouse = l4input_dev_key;
	else
		l4input_dev_mouse = input_allocate_device();
	if (!l4input_dev_key || !l4input_dev_mouse)
		return -ENOMEM;

	/* Keyboard */
	l4input_dev_key->name = singledev ? "L4input" : "L4input key";
	l4input_dev_key->phys = "L4Re::Event";
	l4input_dev_key->uniq = singledev ? "L4input" : "L4input key";
	l4input_dev_key->id.bustype = 0;
	l4input_dev_key->id.vendor  = 0;
	l4input_dev_key->id.product = 0;
	l4input_dev_key->id.version = 0;

	/* We generate key events */
	set_bit(EV_KEY, l4input_dev_key->evbit);
	set_bit(EV_REP, l4input_dev_key->evbit);
	/* We can generate every key, do not use KEY_MAX as apps compiled
	 * against older linux/input.h might have lower values and segfault.
	 * Fun. */
	for (i = 0; i < 0x1ff; i++)
		set_bit(i, l4input_dev_key->keybit);

	if (!singledev) {
		i = input_register_device(l4input_dev_key);
		if (i)
			return i;
	}

	/* Mouse */
	if (!singledev) {
		l4input_dev_mouse->name = "l4input mouse";
		l4input_dev_mouse->phys = "L4Re::Event";
		l4input_dev_mouse->uniq = "l4input mouse";
		l4input_dev_mouse->id.bustype = 0;
		l4input_dev_mouse->id.vendor  = 0;
		l4input_dev_mouse->id.product = 0;
		l4input_dev_mouse->id.version = 0;
	}

	/* We generate key and relative mouse events */
	set_bit(EV_KEY, l4input_dev_mouse->evbit);
	set_bit(EV_REP, l4input_dev_mouse->evbit);
	if (!touchscreen)
		set_bit(EV_REL, l4input_dev_mouse->evbit);
	set_bit(EV_ABS, l4input_dev_mouse->evbit);
	set_bit(EV_SYN, l4input_dev_mouse->evbit);

	/* Buttons */
	if (touchscreen)
		set_bit(BTN_TOUCH,  l4input_dev_mouse->keybit);
	else {
		set_bit(BTN_LEFT,   l4input_dev_mouse->keybit);
		set_bit(BTN_RIGHT,  l4input_dev_mouse->keybit);
		set_bit(BTN_MIDDLE, l4input_dev_mouse->keybit);
		set_bit(BTN_0,      l4input_dev_mouse->keybit);
		set_bit(BTN_1,      l4input_dev_mouse->keybit);
		set_bit(BTN_2,      l4input_dev_mouse->keybit);
		set_bit(BTN_3,      l4input_dev_mouse->keybit);
		set_bit(BTN_4,      l4input_dev_mouse->keybit);
	}

	/* Movements */
	if (!touchscreen) {
		set_bit(REL_X,      l4input_dev_mouse->relbit);
		set_bit(REL_Y,      l4input_dev_mouse->relbit);
	}
	set_bit(ABS_X,      l4input_dev_mouse->absbit);
	set_bit(ABS_Y,      l4input_dev_mouse->absbit);

	set_bit(ABS_PRESSURE,   l4input_dev_mouse->absbit);
	set_bit(ABS_TOOL_WIDTH, l4input_dev_mouse->absbit);
	input_set_abs_params(l4input_dev_mouse, ABS_PRESSURE, 0, 1, 0, 0);

	/* Coordinates are 1:1 pixel in frame buffer */
	input_set_abs_params(l4input_dev_mouse, ABS_X, 0, fbi.width, 0, 0);
	input_set_abs_params(l4input_dev_mouse, ABS_Y, 0, fbi.height, 0, 0);

	i = input_register_device(l4input_dev_mouse);
	if (i)
		return i;

	return 0;
}

static void l4fb_update_dirty_unmap(void)
{
	unsigned int i;
	l4_msg_regs_t *v = l4_utcb_mr_u(l4_utcb());

	i = 0;
	while (i < l4fb_unmap_info.weight) {
		l4_msgtag_t tag;
		l4_addr_t bulkstart, bulksize;
		unsigned int j, num_flexpages;

		num_flexpages = l4fb_unmap_info.weight - i >= L4_UTCB_GENERIC_DATA_SIZE - 2
		                ? L4_UTCB_GENERIC_DATA_SIZE - 2
		                : l4fb_unmap_info.weight - i;

		tag = l4_task_unmap_batch(L4RE_THIS_TASK_CAP, l4fb_unmap_info.flexpages + i,
		                          num_flexpages, L4_FP_ALL_SPACES);

		if (l4_error(tag))
			LOG_printf("l4fb: error with l4_task_unmap_batch\n");

		if (0)
			LOG_printf("l4fb: unmapped %d-%d/%d pages\n", i, i + num_flexpages - 1, l4fb_unmap_info.weight);

#ifdef CONFIG_L4_FB_DRIVER_DBG
		++stats_unmaps;
#endif

		/* redraw dirty pages bulkwise */
		bulksize = 0;
		bulkstart = L4_INVALID_ADDR;
		for (j = 0; j < num_flexpages; j++) {
			l4_fpage_t ret_fp;
			ret_fp.raw = v->mr[2 + j];
			if ((l4_fpage_rights(ret_fp) & L4_FPAGE_W)) {
				if (0)
					LOG_printf("   %d: addr=%lx size=%x was dirty\n",
					           j, l4_fpage_page(ret_fp) << L4_PAGESHIFT,
					           1 << l4_fpage_size(ret_fp));
				if (!bulksize)
					bulkstart = l4_fpage_page(ret_fp) << L4_PAGESHIFT;
				bulksize += 1 << l4_fpage_size(ret_fp);

				continue;
			}
			// we need to flush
			if (bulkstart != L4_INVALID_ADDR)
				l4fb_l4re_update_memarea(bulkstart, bulksize);
			bulksize = 0;
			bulkstart = L4_INVALID_ADDR;
		}
		if (bulkstart != L4_INVALID_ADDR)
			l4fb_l4re_update_memarea(bulkstart, bulksize);

		i += num_flexpages;
	}
}

/* init flexpage array according to map */
static void l4fb_update_dirty_init_flexpages(l4_addr_t addr)
{
	unsigned int log2size, i, j;
	unsigned int size;

	size = l4fb_unmap_info.weight * sizeof(l4_fpage_t);
	if (verbose)
		LOG_printf("l4fb: going to kmalloc(%d) to store flexpages\n", size);
	l4fb_unmap_info.flexpages = kmalloc(size, GFP_KERNEL);
	log2size = l4fb_unmap_info.top;
	for (i = 0; i < l4fb_unmap_info.weight;) {
		for (j = 0; j < l4fb_unmap_info.map[log2size-L4_PAGESHIFT]; ++j) {
			l4fb_unmap_info.flexpages[i] = l4_fpage(addr, log2size, 0);
			if (verbose)
				LOG_printf("%d %lx - %lx \n", i, addr, addr + (1 << log2size));
			addr += 1 << log2size;
			++i;
		}
		--log2size;
	}
}

/* try to reduce the number of flexpages needed (weight) to num_flexpages */
static void l4fb_update_dirty_init_optimize(unsigned int num_flexpages)
{
	while ((num_flexpages > l4fb_unmap_info.weight
	        || l4fb_unmap_info.top > L4_SUPERPAGESHIFT)
	       && l4fb_unmap_info.top >= L4_PAGESHIFT) {
		l4fb_unmap_info.map[l4fb_unmap_info.top - L4_PAGESHIFT] -= 1;
		l4fb_unmap_info.map[l4fb_unmap_info.top - L4_PAGESHIFT - 1] += 2;
		l4fb_unmap_info.weight += 1;
		if (!l4fb_unmap_info.map[l4fb_unmap_info.top - L4_PAGESHIFT])
			l4fb_unmap_info.top -= 1;
	}
	if (verbose)
		LOG_printf("l4fb: optimized on using %d flexpages, %d where requested \n",
		           l4fb_unmap_info.weight, num_flexpages);
}

static void l4fb_update_dirty_init(l4_addr_t addr, l4_addr_t size)
{
	//unsigned int num_flexpages = (L4_UTCB_GENERIC_DATA_SIZE - 2) * unmaps_per_refresh;
	unsigned int num_flexpages = (3 - 2) * unmaps_per_refresh;
	unsigned int log2size = L4_MWORD_BITS - 1;

	memset(&l4fb_unmap_info, 0, sizeof(l4fb_unmap_info));
	size = l4_round_page(size);

	/* init map with bitlevel number */
	while (log2size >= L4_PAGESHIFT) {
		if (size & (1 << log2size)) {
			l4fb_unmap_info.map[log2size-L4_PAGESHIFT] = 1;
			if (!l4fb_unmap_info.top)
				l4fb_unmap_info.top = log2size;
			l4fb_unmap_info.weight += 1;
		}
		--log2size;
	}
	l4fb_update_dirty_init_optimize(num_flexpages);
	l4fb_update_dirty_init_flexpages(addr);
}


static void l4fb_refresh_func(unsigned long data)
{
	if (l4fb_refresh_enabled && l4fb_update_rect) {
		if (1)
			l4fb_update_rect(0, 0, fbi.width, fbi.height);
		else
			l4fb_update_dirty_unmap();
	}

	mod_timer(&refresh_timer, jiffies + l4fb_refresh_sleep);
}

/* ============================================ */

static int l4fb_fb_init(struct fb_var_screeninfo *var,
                        struct fb_fix_screeninfo *fix)
{
	int ret, input_avail = 0;
	L4XV_V(f);

	L4XV_L(f);

	if (verbose)
		LOG_printf("Starting L4FB\n");

	ret = l4re_util_video_goos_fb_setup_name(&goos_fb, "fb");
	if (ret < 0)
		goto out_unlock;

	ret = -ENOMEM;
	if (l4re_util_video_goos_fb_view_info(&goos_fb, &fbi))
		goto out_unlock;

	if (l4_is_invalid_cap(ev_ds = l4x_cap_alloc()))
		goto out_unlock;

	if (l4_is_invalid_cap(ev_irq = l4x_cap_alloc())) {
		l4x_cap_free(ev_ds);
		goto out_unlock;
	}

	if (l4re_event_get(l4re_util_video_goos_fb_goos(&goos_fb), ev_ds)) {
		LOG_printf("l4fb: INFO: No input available\n");

		l4x_cap_free(ev_ds);
		l4x_cap_free(ev_irq);
	} else {
		input_avail = 1;
		ret = -ENOENT;
		if (l4re_event_buffer_attach(&ev_buf, ev_ds, l4re_env()->rm))
			goto out_unlock;

		if (l4_error(l4_factory_create_irq(l4re_env()->factory, ev_irq))) {
			l4x_cap_free(ev_ds);
			l4x_cap_free(ev_irq);
			goto out_unlock;
		}

		if (l4_error(l4_icu_bind(l4re_util_video_goos_fb_goos(&goos_fb),
						0, ev_irq))) {
			l4re_util_cap_release(ev_irq);
			l4x_cap_free(ev_ds);
			l4x_cap_free(ev_irq);
			goto out_unlock;
		}
	}

	L4XV_U(f);

	var->xres           = fbi.width;
	var->yres           = fbi.height;
	var->bits_per_pixel = l4re_video_bits_per_pixel(&fbi.pixel_info);

	/* The console expects the real screen resolution, not the virtual */
	fix->line_length    = fbi.bytes_per_line;
	L4XV_L(f);
	fix->smem_len       = l4_round_page(l4re_ds_size(l4re_util_video_goos_fb_buffer(&goos_fb)));
	L4XV_U(f);
	fix->visual         = FB_VISUAL_TRUECOLOR;

	/* We cannot really set (smaller would work) screen paramenters
	 * when using con */
	if (var->bits_per_pixel == 15)
		var->bits_per_pixel = 16;

	var->red.offset = fbi.pixel_info.r.shift;
	var->red.length = fbi.pixel_info.r.size;
	var->green.offset = fbi.pixel_info.g.shift;
	var->green.length = fbi.pixel_info.g.size;
	var->blue.offset = fbi.pixel_info.b.shift;
	var->blue.length = fbi.pixel_info.b.size;

	L4XV_L(f);
	LOG_printf("l4fb: %dx%d@%d %dbypp, size: %d\n",
	           var->xres, var->yres, var->bits_per_pixel,
	           fbi.pixel_info.bytes_per_pixel, fix->smem_len);
	LOG_printf("l4fb: %d:%d:%d %d:%d:%d linelen=%d visual=%d\n",
	           var->red.length, var->green.length, var->blue.length,
	           var->red.offset, var->green.offset, var->blue.offset,
	           fix->line_length, fix->visual);


	fix->smem_start = 0;
	ret = -ENOMEM;
	if (l4re_rm_attach((void **)&fix->smem_start,
	                   fix->smem_len + L4_PAGESIZE*10,
	                   L4RE_RM_SEARCH_ADDR,
	                   l4re_util_video_goos_fb_buffer(&goos_fb),
	                   0, L4_SUPERPAGESHIFT))
		goto out_unlock;

	LOG_printf("l4fb: fb memory: %lx - %lx\n",
	           fix->smem_start, fix->smem_start + fix->smem_len);
	L4XV_U(f);

	// remember start and size of framebuffer for use in dirty update
	// routine and for shutdown... (works only for one fb...)
	_fb_addr        = fix->smem_start;
	_fb_line_length = fix->line_length;
	l4fb_update_dirty_init(fix->smem_start, fix->smem_len);

	if (l4fb_refresh_sleep) {
		init_timer(&refresh_timer);
		refresh_timer.function = l4fb_refresh_func;
		refresh_timer.expires  = jiffies + l4fb_refresh_sleep;
		add_timer(&refresh_timer);
	}

	if (input_avail)
		return l4fb_input_setup();
	return 0;

out_unlock:
	L4XV_U(f);
	return ret;
}

/* ============================================ */

static void l4fb_shutdown(void)
{
	L4XV_V(f);
	del_timer_sync(&refresh_timer);

	/* Also do not update anything anymore */
	l4fb_update_rect = NULL;

	free_irq(irqnum, NULL);
	l4x_unregister_irq(irqnum);

	L4XV_L(f);
	l4re_rm_detach((void *)_fb_addr);
	l4re_util_video_goos_fb_destroy(&goos_fb);

	if (l4_is_valid_cap(ev_irq)) {
		l4re_util_cap_release(ev_irq);
		l4x_cap_free(ev_irq);
	}
	if (l4_is_valid_cap(ev_ds)) {
		l4re_util_cap_release(ev_ds);
		l4x_cap_free(ev_ds);
	}
	L4XV_U(f);
}

static int l4fb_shutdown_sysdev(struct sys_device *dev)
{
	//l4fb_shutdown();
	// We cannot call shutdown here since it seems we cannot remove the
	// timer as this subsystem already seems to be done (at least it's
	// spinning in kernel/timer.c), and we also cannot remove the other
	// parts since the timer function is still being called...
	// Maybe we should try again with some later Linux version
	return 0;
}

static struct sysdev_class l4fb_sysdev_class = {
	.name = "l4fb",
	.shutdown = l4fb_shutdown_sysdev,
};

static struct sys_device device_l4fb = {
	.id     = 0,
	.cls    = &l4fb_sysdev_class,
};

static int __init l4fb_sysfs_init(void)
{
	int e = sysdev_class_register(&l4fb_sysdev_class);
	if (!e)
		e = sysdev_register(&device_l4fb);
	return e;
}

static int __init l4fb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int video_cmap_len;
	int ret = -ENOMEM;

	if (disable)
		return -ENODEV;

	/* Process module parameters */
	if (refreshsleep >= 0) {
		u64 t = HZ * refreshsleep;
		do_div(t, 1000);
		l4fb_refresh_sleep = t;
	}

	l4fb_update_rect = l4fb_l4re_update_rect;

	info = framebuffer_alloc(0, &dev->dev);
	if (!info)
		return -ENOMEM;

	info->fbops = &l4fb_ops;
	info->var   = l4fb_defined;
	info->fix   = l4fb_fix;
	info->pseudo_palette = pseudo_palette;
	info->flags = FBINFO_FLAG_DEFAULT;


	ret = l4fb_fb_init(&info->var, &info->fix);
	if (ret) {
		if (verbose)
			LOG_printf("init error %d\n", ret);
		goto failed_after_framebuffer_alloc;
	}

	info->screen_base = (void *)info->fix.smem_start;
	if (!info->screen_base) {
		printk(KERN_ERR "l4fb: abort, graphic system could not be initialized.\n");
		ret = -EIO;
		goto failed_after_framebuffer_alloc;
	}

	printk(KERN_INFO "l4fb: Framebuffer at 0x%p, size %dk\n",
	       info->screen_base, info->fix.smem_len >> 10);
	printk(KERN_INFO "l4fb: mode is %dx%dx%d, linelength=%d, pages=%d\n",
	       info->var.xres, info->var.yres, info->var.bits_per_pixel,
	       info->fix.line_length, screen_info.pages);

	info->var.xres_virtual = info->var.xres;
	info->var.yres_virtual = info->var.yres;

	/* some dummy values for timing to make fbset happy */
	info->var.pixclock     = 10000000 / info->var.xres * 1000 / info->var.yres;
	info->var.left_margin  = (info->var.xres / 8) & 0xf8;
	info->var.hsync_len    = (info->var.xres / 8) & 0xf8;

	info->var.transp.length = 0;
	info->var.transp.offset = 0;

	printk(KERN_INFO "l4fb: directcolor: "
	       "size=%d:%d:%d:%d, shift=%d:%d:%d:%d\n",
	       0,
	       info->var.red.length,
	       info->var.green.length,
	       info->var.blue.length,
	       0,
	       info->var.red.offset,
	       info->var.green.offset,
	       info->var.blue.offset);
	video_cmap_len = 16;

	info->fix.ypanstep  = 0;
	info->fix.ywrapstep = 0;

	ret = fb_alloc_cmap(&info->cmap, video_cmap_len, 0);
	if (ret < 0)
		goto failed_after_framebuffer_alloc;

	if (register_framebuffer(info) < 0) {
		ret = -EINVAL;
		goto failed_after_fb_alloc_cmap;
	}
	dev_set_drvdata(&dev->dev, info);

	l4fb_sysfs_init();

	printk(KERN_INFO "l4fb%d: %s L4 frame buffer device (refresh: %ujiffies)\n",
	       info->node, info->fix.id, l4fb_refresh_sleep);

	return 0;

failed_after_fb_alloc_cmap:
	fb_dealloc_cmap(&info->cmap);

failed_after_framebuffer_alloc:
	framebuffer_release(info);

	return ret;
}

static int l4fb_remove(struct platform_device *device)
{
	struct fb_info *info = platform_get_drvdata(device);

	if (info) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);

		sysdev_register(&device_l4fb);
		sysdev_class_unregister(&l4fb_sysdev_class);

		l4fb_shutdown();
	}
	return 0;
}

static struct platform_driver l4fb_driver = {
	.probe   = l4fb_probe,
	.remove  = l4fb_remove,
	.driver  = {
		.name = "l4fb",
	},
};

static struct platform_device l4fb_device = {
	.name = "l4fb",
};

static int __init l4fb_init(void)
{
	int ret;

	ret = platform_driver_register(&l4fb_driver);
	if (!ret) {
		ret = platform_device_register(&l4fb_device);
		if (ret)
			platform_driver_unregister(&l4fb_driver);
	}
#ifdef CONFIG_L4_FB_DRIVER_DBG
	if (!IS_ERR(l4x_debugfs_dir))
		debugfs_dir = debugfs_create_dir("l4fb", NULL);
	if (!IS_ERR(debugfs_dir)) {
		debugfs_unmaps  = debugfs_create_u32("unmaps", S_IRUGO,
		                                     debugfs_dir, &stats_unmaps);
		debugfs_updates = debugfs_create_u32("updates", S_IRUGO,
		                                     debugfs_dir, &stats_updates);
	}
#endif
	return ret;
}
module_init(l4fb_init);

static void __exit l4fb_exit(void)
{
#ifdef CONFIG_L4_FB_DRIVER_DBG
	debugfs_remove(debugfs_unmaps);
	debugfs_remove(debugfs_updates);
	debugfs_remove(debugfs_dir);
#endif

	kfree(l4fb_unmap_info.flexpages);
	input_unregister_device(l4input_dev_key);
	if (!singledev)
		input_unregister_device(l4input_dev_mouse);
	platform_device_unregister(&l4fb_device);
	platform_driver_unregister(&l4fb_driver);
}
module_exit(l4fb_exit);

MODULE_AUTHOR("Adam Lackorzynski <adam@os.inf.tu-dresden.de>");
MODULE_DESCRIPTION("Frame buffer driver for L4Re::Console's");
MODULE_LICENSE("GPL");


module_param(refreshsleep, int, 0444);
MODULE_PARM_DESC(refreshsleep, "Sleep between frame buffer refreshs in ms");
module_param(disable, bool, 0);
MODULE_PARM_DESC(disable, "Disable driver");
module_param(touchscreen, bool, 0);
MODULE_PARM_DESC(touchscreen, "Be a touchscreen");
module_param(singledev, bool, 0);
MODULE_PARM_DESC(singledev, "Expose only one input device");
module_param(abs2rel, bool, 0);
MODULE_PARM_DESC(abs2rel, "Convert absolute events to relative ones");
module_param(verbose, bool, 0);
MODULE_PARM_DESC(verbose, "Tell more");
