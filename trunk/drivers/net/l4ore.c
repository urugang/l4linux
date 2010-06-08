/*
 * Ore network driver stub.
 */

#include <linux/etherdevice.h>

#include <asm/l4lxapi/misc.h>
#include <asm/l4lxapi/thread.h>

#include <asm/generic/l4lib.h>
#include <asm/generic/setup.h>
#include <asm/generic/do_irq.h>

#include <l4/ore/ore.h>

MODULE_AUTHOR("Adam Lackorzynski <adam@os.inf.tu-dresden.de>");
MODULE_DESCRIPTION("Ore stub driver");
MODULE_LICENSE("GPL");

L4_EXTERNAL_FUNC(l4ore_open);
L4_EXTERNAL_FUNC(l4ore_send);
L4_EXTERNAL_FUNC(l4ore_recv_blocking);
L4_EXTERNAL_FUNC(l4ore_close);
L4_EXTERNAL_FUNC(l4ore_set_config);

static int  l4x_ore_numdevs = 1;
#define MAX_OREINST 6
static char *l4x_ore_instances[MAX_OREINST] = { "ORe:eth0", 0, 0, 0, 0, 0 };
static LIST_HEAD(l4x_ore_netdevices);

struct l4x_ore_priv {
	struct net_device_stats    net_stats;

	int                        handle;
	l4_cap_idx_t               rx_sig;
	l4ore_config               config;
	unsigned char              *pkt_buffer;
	unsigned long              pkt_size;

	l4_threadid_t              irq_thread;
	struct irq_chip            *previous_interrupt_type;
};

struct l4x_ore_netdev {
	struct list_head  list;
	struct net_device *dev;
};

static int l4x_ore_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct l4x_ore_priv *priv = netdev_priv(netdev);
	short length = skb->len;

	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return 0;
		length = ETH_ZLEN;
	}

	if (l4ore_send(priv->handle, (char *)skb->data, length) < 0) {
		LOG_printf("%s: send failed\n", netdev->name);
		return 1; /* Hmm, which return type to take? */
	}

	dev_kfree_skb(skb);

	netdev->trans_start = jiffies;
	priv->net_stats.tx_packets++;
	priv->net_stats.tx_bytes += skb->len;

	return 0;
}

static struct net_device_stats *l4x_ore_get_stats(struct net_device *netdev)
{
	struct l4x_ore_priv *priv = netdev_priv(netdev);
	return &priv->net_stats;
}

static void l4x_ore_tx_timeout(struct net_device *netdev)
{
	LOG_printf("%s\n", __func__);
}

/*
 * Interrupt.
 */
static irqreturn_t l4x_ore_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct l4x_ore_priv *priv = netdev_priv(netdev);
	struct sk_buff *skb;

	/* if packet size is zero, this interface did not cause the interrupt */
	if (!priv->pkt_size)
		return IRQ_HANDLED;

	skb = dev_alloc_skb(priv->pkt_size);
	if (likely(skb)) {
		skb->dev = netdev;
		memcpy(skb_put(skb, priv->pkt_size),
		       priv->pkt_buffer, priv->pkt_size);

		/* reset packet size to zero - we do this since we provide
		 * several interfaces sharing the same IRQ and we need to
		 * detect on which interface the current IRQ occured */
		priv->pkt_size = 0;
		skb->protocol = eth_type_trans(skb, netdev);
		netif_rx(skb);

		netdev->last_rx = jiffies;
		priv->net_stats.rx_bytes += skb->len;
		priv->net_stats.rx_packets++;

	} else {
		printk(KERN_WARNING "%s: dropping packet.\n", netdev->name);
		priv->net_stats.rx_dropped++;
	}

	return IRQ_HANDLED;
}

/*
 * Receive thread to get packets
 */
static L4_CV void l4x_ore_irq_thread(void *data)
{
	struct net_device *netdev = *(struct net_device **)data;
	struct l4x_ore_priv *priv = netdev_priv(netdev);
	int ret;
	struct thread_info *ctx = current_thread_info();

	l4x_prepare_irq_thread(ctx, 0);

	while (1) {
		unsigned int size = ETH_HLEN + netdev->mtu;
		ret = l4ore_recv_blocking(priv->handle,
		                          (char **)&priv->pkt_buffer, &size,
		                          L4_IPC_NEVER);
		if (unlikely(ret < 0)) {
			LOG_printf("%s: l4ore_recv_blocking failed: %d\n",
			           netdev->name, ret);
			l4_sleep(100);
			continue;
		} else if (unlikely(ret > 0))
			LOG_printf("%s: buffer too small (%d)\n", netdev->name, ret);

		priv->pkt_size = size;
		l4x_do_IRQ(netdev->irq, ctx);
	}
}

/* ----- */
static unsigned int l4x_ore_irq_startup(unsigned int irq)
{
	LOG_printf("%s\n", __func__);
	return 1;
}

static void l4x_ore_irq_dummy_void(unsigned int irq)
{
}

struct irq_chip l4x_ore_irq_type = {
	.name		= "L4Ore IRQ",
	.startup	= l4x_ore_irq_startup,
	.shutdown	= l4x_ore_irq_dummy_void,
	.enable		= l4x_ore_irq_dummy_void,
	.disable	= l4x_ore_irq_dummy_void,
	.mask		= l4x_ore_irq_dummy_void,
	.unmask		= l4x_ore_irq_dummy_void,
	.ack		= l4x_ore_irq_dummy_void,
	.end		= l4x_ore_irq_dummy_void,
};
/* ----- */

static int l4x_ore_open(struct net_device *netdev)
{
	struct l4x_ore_priv *priv = netdev_priv(netdev);
	int err = -ENODEV;

	netif_carrier_off(netdev);

	if (!priv->config.rw_active) {
		priv->config.rw_active = 1;
		l4ore_set_config(priv->handle, &priv->config);
	}

	printk("%s: Overwriting IRQ type for IRQ %d with l4ore type!\n",
	       netdev->name, netdev->irq);

	priv->previous_interrupt_type = irq_desc[netdev->irq].chip;

	if (netdev->irq < NR_IRQS)
		irq_desc[netdev->irq].chip = &l4x_ore_irq_type;
	else {
		printk("%s: irq(%d) > NR_IRQS(%d), failing\n",
		       netdev->name, netdev->irq, NR_IRQS);
		goto err_out_close;
	}

	priv->pkt_buffer = kmalloc(ETH_HLEN + netdev->mtu, GFP_KERNEL);
	if (!priv->pkt_buffer) {
		printk("%s: kmalloc error\n", netdev->name);
		goto err_out_close;
	}

	if ((err = request_irq(netdev->irq, l4x_ore_interrupt,
	                       IRQF_SAMPLE_RANDOM | IRQF_SHARED,
	                       netdev->name, netdev))) {
		printk("%s: request_irq(%d, ...) failed: %d\n",
		       netdev->name, netdev->irq, err);
		goto err_out_kfree;
	}

	priv->irq_thread = l4lx_thread_create(l4x_ore_irq_thread, 0,
	                                      NULL, &netdev, sizeof(netdev),
	                                      CONFIG_L4_PRIO_L4ORE, "L4OreRcv");
	if (l4_is_invalid_id(priv->irq_thread)) {
		printk("%s: Cannot create thread\n", netdev->name);
		err = -EBUSY;
		goto err_out_free_irq;
	}

	netif_carrier_on(netdev);
	netif_wake_queue(netdev);

	printk("%s: interface up.\n", netdev->name);

	return 0;

err_out_free_irq:
	free_irq(netdev->irq, netdev);

err_out_kfree:
	kfree(priv->pkt_buffer);

err_out_close:
	irq_desc[netdev->irq].chip = priv->previous_interrupt_type;
	l4ore_close(priv->handle);
	priv->config.rw_active = 0;
	priv->handle           = 0;
	return err;
}

static int l4x_ore_close(struct net_device *netdev)
{
	struct l4x_ore_priv *priv = netdev_priv(netdev);

	l4lx_thread_shutdown(priv->irq_thread);
	priv->irq_thread = L4_INVALID_ID;

	free_irq(netdev->irq, netdev);
	irq_desc[netdev->irq].chip = priv->previous_interrupt_type;
	netif_stop_queue(netdev);
	netif_carrier_off(netdev);

	kfree(priv->pkt_buffer);

	priv->config.rw_active = 0;

	return 0;
}

/*
 * Split 'inst:foo' into separate strings 'inst' and 'foo'
 */
static int l4x_ore_parse_instance(int id, char *inst, int instsize,
                                          char *dev, int devsize)
{
	char *string = l4x_ore_instances[id];
	char *s2;

	s2 = strsep(&string, ":");
	if (!s2 || !string)
		return -1;
	strcpy(inst, s2);
	strcpy(dev,  string);
	inst[instsize - 1] = 0;
	dev[devsize - 1] = 0;

	return 0;
}

static const struct net_device_ops l4ore_netdev_ops = {
	.ndo_open       = l4x_ore_open,
	.ndo_start_xmit = l4x_ore_xmit_frame,
	.ndo_stop       = l4x_ore_close,
	.ndo_tx_timeout = l4x_ore_tx_timeout,
	.ndo_get_stats  = l4x_ore_get_stats,
};

/* Initialize one virtual interface. */
static int __init l4x_ore_init_device(char *oreinst, char *devname)
{
	struct l4x_ore_priv *priv;
	struct net_device *dev = NULL;
	struct l4x_ore_netdev *nd = NULL;
	int err = -ENODEV;
	DECLARE_MAC_BUF(macstring);

	if (!(dev = alloc_etherdev(sizeof(struct l4x_ore_priv))))
		return -ENOMEM;

	dev->netdev_ops = &l4ore_netdev_ops;
	priv = netdev_priv(dev);

	priv->config = L4ORE_DEFAULT_CONFIG;
	priv->config.rw_debug           = 0;
	priv->config.rw_broadcast       = 1;
	// lo devices have useless MAC 00:00:00:00:00:00
	if (strcmp(devname, "lo"))
		priv->config.ro_keep_device_mac = 1;
	else
		priv->config.ro_keep_device_mac = 0;
	priv->config.rw_active          = 0;
	strncpy(priv->config.ro_orename, oreinst, sizeof(priv->config.ro_orename));
	priv->config.ro_orename[sizeof(priv->config.ro_orename)-1] = 0;

	/* Hmm, we need to open the connection here to get the MAC :/ */
	if ((priv->handle = l4ore_open(devname, dev->dev_addr, &priv->config)) < 0) {
		printk("%s: l4ore_open failed: %d\n",
		       dev->name, priv->handle);
		goto err_out_free_dev;
	}

	if ((dev->irq = l4x_register_irq(&priv->rx_sig)) < 0) {
		printk("Failed to get signal irq\n");
		goto err_out_free_dev;
	}
	dev->mtu = priv->config.ro_mtu;

	if ((err = register_netdev(dev))) {
		printk("l4ore: Cannot register net device, aborting.\n");
		goto err_out_free_dev;
	}

	nd = kmalloc(sizeof(struct l4x_ore_netdev), GFP_KERNEL);
	if (!nd) {
		printk("Out of memory.\n");
		return -1;
	}
	nd->dev = dev;
	list_add(&nd->list, &l4x_ore_netdevices);

	printk(KERN_INFO "%s: L4Ore card found with %s, IRQ %d\n",
	                 dev->name, print_mac(macstring, dev->dev_addr),
	                 dev->irq);

	return 0;

err_out_free_dev:
	free_netdev(dev);

	return err;
}

static int __init l4x_ore_init(void)
{
	int i = 0;
	int num_init = 0;

	LOG_printf("Creating %d ORe device(s).\n", l4x_ore_numdevs);

	for (i = 0; i < l4x_ore_numdevs; i++) {
		char instbuf[16], devbuf[16];
		int ret = l4x_ore_parse_instance(i, instbuf, sizeof(instbuf),
		                                    devbuf, sizeof(devbuf));
		if (!ret) {
			LOG_printf("Opening device %s at ORe instance %s\n",
			           devbuf, instbuf);
			ret = l4x_ore_init_device(instbuf, devbuf);
			if (!ret)
				num_init++;
		}
		else
			LOG_printf("Invalid device string: %s\n",
			           l4x_ore_instances[i]);
	}

	return num_init > 0 ? 0 : -1;
}

static void __exit l4x_ore_exit(void)
{
	struct list_head *p, *n;
	list_for_each_safe(p, n, &l4x_ore_netdevices) {
		struct l4x_ore_netdev *nd
		  = list_entry(p, struct l4x_ore_netdev, list);
		struct net_device *dev = nd->dev;
		l4ore_close(((struct l4x_ore_priv *)netdev_priv(dev))->handle);
		unregister_netdev(dev);
		free_netdev(dev);
		list_del(p);
		kfree(nd);
	}
}

module_init(l4x_ore_init);
module_exit(l4x_ore_exit);

module_param_array_named(instances, l4x_ore_instances, charp, &l4x_ore_numdevs, 0);
MODULE_PARM_DESC(oreinstances, "ORe instances to connect to");
