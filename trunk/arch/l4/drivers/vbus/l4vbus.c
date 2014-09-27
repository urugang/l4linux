#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <l4/vbus/vbus.h>
#include <l4/re/env.h>

#include <asm/generic/l4lib.h>
#include <asm/generic/event.h>
#include <asm/generic/vbus.h>

L4_EXTERNAL_FUNC(l4vbus_get_hid);
L4_EXTERNAL_FUNC(l4vbus_get_next_device);

static l4_cap_idx_t vbus;
static struct l4x_event_source vbus_event_source;

static void process_bus_event(struct l4x_event_stream *stream,
                              l4re_event_t const *event)
{
	struct l4x_vbus_device *dev = l4x_vbus_device_from_stream(stream);
	struct l4x_vbus_driver *drv;

	if (!dev->dev.driver)
		return;

	BUG_ON(dev->dev.driver->bus != &l4x_vbus_bus_type);
	drv = l4x_vbus_driver_from_driver(dev->dev.driver);

	if (drv->ops.notify)
		drv->ops.notify(dev, event->type, event->code, event->value);
}

static struct l4x_event_stream *
bus_unknown_event_stream(struct l4x_event_source *src, unsigned long id)
{
	pr_info("l4vbus: no driver for vbus id: %lx registered\n", id);
	return NULL;
}

static void bus_free_event_stream(struct l4x_event_source *src,
                                  struct l4x_event_stream *stream)
{
}

static struct l4x_event_source_ops vbus_event_ops = {
	.process     = process_bus_event,
	.new_stream  = bus_unknown_event_stream,
	.free_stream = bus_free_event_stream
};

static int l4x_vbus_match(struct device *dev, struct device_driver *drv)
{
	struct l4x_vbus_driver *driver = l4x_vbus_driver_from_driver(drv);
	struct l4x_vbus_device *device = l4x_vbus_device_from_device(dev);
	const struct l4x_vbus_device_id *id;

	for (id = driver->id_table; id->cid; ++id)
		if (!strcmp(device->hid, id->cid))
			return 1;

	return 0;
}

static int l4x_vbus_probe(struct device *dev)
{
	struct l4x_vbus_device *device = l4x_vbus_device_from_device(dev);
	struct l4x_vbus_driver *driver = l4x_vbus_driver_from_driver(dev->driver);
	int res;

	if (!driver->ops.add)
		return -ENOSYS;

	res = driver->ops.add(device);
	if (res < 0)
		return res;

	if (driver->ops.notify) {
		device->ev_stream.id = device->vbus_handle;
		res = l4x_event_source_register_stream(&vbus_event_source,
		                                       &device->ev_stream);
		if (res < 0)
			return res;
	}

	return 0;
}

static int l4x_vbus_remove(struct device *dev)
{
	struct l4x_vbus_device *device = l4x_vbus_device_from_device(dev);
	struct l4x_vbus_driver *driver;

	driver = l4x_vbus_driver_from_driver(dev->driver);

	if (driver->ops.notify)
		l4x_event_source_unregister_stream(&vbus_event_source,
		                                   &device->ev_stream);

	if (driver->ops.remove)
		driver->ops.remove(device);

	device->driver_data = NULL;
	put_device(dev);
	return 0;
}

struct bus_type l4x_vbus_bus_type = {
	.name   = "l4vbus",
	.match  = l4x_vbus_match,
	.probe  = l4x_vbus_probe,
	.remove = l4x_vbus_remove,
};

static int vbus_scan(struct device *parent, l4vbus_device_handle_t parent_hdl);

static int create_vbus_device(struct device *parent, l4vbus_device_handle_t handle, char const *name)
{
	int err;
	struct l4x_vbus_device *dev;

	dev = kzalloc(sizeof(struct l4x_vbus_device), GFP_KERNEL);
	if (!dev) {
		pr_err("l4vbus: cannot allocate device\n");
		return -ENOMEM;
	}

	dev->vbus_handle = handle;
	dev->dev.parent = parent;
	dev->dev.bus = &l4x_vbus_bus_type;

	err = L4XV_FN_i(l4vbus_get_hid(vbus, handle,
	                               dev->hid, sizeof(dev->hid)));
	if (err < 0) {
		pr_warn("l4vbus: cannot get HID for device %s:%lx: err=%d\n",
		        name, handle, err);
		dev->hid[0] = 0;
	} else
		dev->hid[sizeof(dev->hid) - 1] = 0;

	dev_set_name(&dev->dev, "%s:%s:%lx", dev->hid, name, handle);

	if ((err = device_register(&dev->dev)) < 0) {
		pr_err("l4vbus: cannot register device\n");
		kfree(dev);
		return err;
	}
	vbus_scan(&dev->dev, handle);
	return 0;
}

static int vbus_scan(struct device *parent, l4vbus_device_handle_t parent_hdl)
{
	l4vbus_device_handle_t cur = 0;
	l4vbus_device_t devinfo;

	while (!L4XV_FN_i(l4vbus_get_next_device(vbus, parent_hdl,
	                                         &cur, 0, &devinfo))) {
		int err = create_vbus_device(parent, cur, devinfo.name);
		if (err)
			pr_err("l4vbus: could not create device %lx: %d\n",
			       cur, err);
	}
	return 0;
}

static int __init vbus_init(void)
{
	int err;

	vbus = l4re_env_get_cap("vbus");
	if (l4_is_invalid_cap(vbus))
		return -ENOENT;

	err = bus_register(&l4x_vbus_bus_type);
	if (err < 0)
		return err;

	pr_info("l4vbus: is running\n");

	vbus_event_source.event_cap = vbus;
	l4x_event_setup_source(&vbus_event_source, 1 << 8, &vbus_event_ops);
	return 0;
}

subsys_initcall(vbus_init);

static int __init vbus_devs_init(void)
{
	if (!l4_is_invalid_cap(vbus))
		create_vbus_device(NULL, 0, "system");
	return 0;
}

device_initcall(vbus_devs_init);
