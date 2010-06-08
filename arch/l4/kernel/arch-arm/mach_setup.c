/*
 * Device specific code.
 */

#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/smsc911x.h>
#include <linux/slab.h>

#include <asm/generic/io.h>
#include <asm/generic/devs.h>

#include <linux/list.h>

static int dev_init_done;

struct platform_data_elem {
	struct list_head list;
	void *data;
	const char *l4io_name;
	const char *linux_name;
};

static LIST_HEAD(platform_data_head);

int __init l4x_register_platform_data(const char *l4io_name,
                                      const char *linux_name,
                                      void *platformdata)
{
	struct platform_data_elem *e;

	if (dev_init_done) {
		printk(KERN_ERR "Registering of platform data for '%s' too late!\n", linux_name);
		return -EINVAL;
	}

	e = kmalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->l4io_name  = l4io_name;
	e->linux_name = linux_name;
	e->data       = platformdata;

	list_add(&e->list, &platform_data_head);

	return 0;
}

static void __init free_platform_data_elems(void)
{
	struct platform_data_elem *e, *r;
	list_for_each_entry_safe(e, r, &platform_data_head, list) {
		list_del(&e->list);
		kfree(e);
	}
}

/* Any additional platform_data information goes here */
static struct pata_platform_info pata_platform_data = {
	.ioport_shift           = 1,
};

static struct smsc911x_platform_config smsc911x_config = {
	.flags          = SMSC911X_USE_32BIT,
	.irq_polarity   = SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
	.irq_type       = SMSC911X_IRQ_TYPE_PUSH_PULL,
	.phy_interface  = PHY_INTERFACE_MODE_MII,
};

static void register_platform_data_default(void)
{
	l4x_register_platform_data("compactflash", "pata_platform", &pata_platform_data);
	l4x_register_platform_data("smsc911x",     "smsc911x",      &smsc911x_config);
}



unsigned long l4x_arm_dma_mem_phys_start, l4x_arm_dma_mem_size;
l4_addr_t l4x_arm_dma_mem_virt_start;


static void __init dmamem_voodoo(l4io_device_handle_t devhandle,
                                 l4io_resource_handle_t reshandle)
{
	l4io_resource_t res;
	if (l4io_lookup_resource(devhandle, L4IO_RESOURCE_MEM,
	                         &reshandle, &res)) {
		printk(KERN_ERR "No DMA memory?\n");
		return;
	}

	l4x_arm_dma_mem_phys_start = res.start;
	l4x_arm_dma_mem_size       = res.end - res.start + 1;
	printk("DMA mem phys at %08lx - %08lx\n", res.start, res.end);


	if (l4io_request_iomem(res.start, res.end - res.start,
	                       L4IO_MEM_NONCACHED ,
	                       &l4x_arm_dma_mem_virt_start)) {
		printk(KERN_ERR "Could not get DMA MEM\n");
		l4x_arm_dma_mem_phys_start = 0;
		l4x_arm_dma_mem_virt_start = 0;
		return;
	}
	printk("DMA mem virt at %08lx - %08lx\n",
	       l4x_arm_dma_mem_virt_start,
	       l4x_arm_dma_mem_virt_start + l4x_arm_dma_mem_size - 1);
}


static void __init add_platform_device(l4io_device_handle_t devhandle,
                                       l4io_resource_handle_t reshandle,
                                       unsigned num_res,
                                       const char *name, int id)
{
	l4io_resource_t res;
	struct platform_device *platdev;
	struct resource *platres;
	struct platform_data_elem *e;
	void *pdata = NULL;
	int i;
	L4XV_V(f);

	list_for_each_entry(e, &platform_data_head, list)
		if (!strcmp(e->l4io_name, name)) {
			name = e->linux_name;
			pdata = e->data;
			break;
		}

	platdev = platform_device_alloc(name, id);
	if (!platdev)
		return;

	if (pdata)
		platdev->dev.platform_data = pdata;

	platres = kzalloc(sizeof(*platres) * num_res, GFP_KERNEL);
	if (!platres)
		return;

	platdev->num_resources = num_res;
	platdev->resource      = platres;

	i = 0;
	L4XV_L(f);
	while (!l4io_lookup_resource(devhandle, L4IO_RESOURCE_ANY,
	                             &reshandle, &res)) {
		BUG_ON(i > platdev->num_resources);

		switch (res.type) {
			case L4IO_RESOURCE_IRQ:
				platres[i].flags = IORESOURCE_IRQ;
				break;
			case L4IO_RESOURCE_MEM:
				platres[i].flags = IORESOURCE_MEM;
				break;
			case L4IO_RESOURCE_PORT:
				platres[i].flags = IORESOURCE_IO;
				break;
			default:
				printk("Platform device: %s: Type %x unknown\n",
				       name, res.type);
				platres[i].flags = IORESOURCE_DISABLED;
				break;
		};

		platres[i].start = res.start;
		platres[i].end   = res.end;
		++i;
	}
	L4XV_U(f);

	if (platform_device_add(platdev)) {
		printk("Adding of static platform device '%s' failed.\n", name);
		return;
	}

	printk("Added static device %s with %d resources.\n",
	       name, platdev->num_resources);

}

#include <linux/amba/bus.h>
#include <asm/irq.h>

static struct amba_device aacidev = {
	.dev = {
		.coherent_dma_mask = ~0,
		.init_name = "busid",
		.platform_data = NULL,
	},
	.res = {
		.start = 0x10004000,
		.end   = 0x10004fff,
		.flags  = IORESOURCE_MEM,
	},
	.dma_mask       = ~0,
	.irq            = {32, NO_IRQ },
};

void __init l4x_arm_devices_init(void)
{
	l4io_device_handle_t dh = l4io_get_root_device();
	l4io_device_t dev;
	l4io_resource_handle_t reshandle;
	L4XV_V(f);

	register_platform_data_default();

	dev_init_done = 1;

	while (1) {
		L4XV_L(f);
		if (l4io_iterate_devices(&dh, &dev, &reshandle))
			break;
		L4XV_U(f);

		if (dev.num_resources == 0)
			continue;

		if (!strcmp(dev.name, "aaci")) {
			printk("AACI test\n");

			amba_device_register(&aacidev, &iomem_resource);

			continue;
		}

		if (!strcmp(dev.name, "DMAMEM")) {
			printk("FOUND DMAMEM\n");
			dmamem_voodoo(dh, reshandle);
			continue;
		}

		add_platform_device(dh, reshandle, dev.num_resources,
		                    dev.name, 0);
	}
	L4XV_U(f);

	free_platform_data_elems();
}
