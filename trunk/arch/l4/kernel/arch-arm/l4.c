#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/err.h>

#include <asm/mach-types.h>
#include <asm/delay.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#include <asm/setup.h>

#include <asm/l4lxapi/irq.h>
#include <asm/api/config.h>
#include <asm/generic/irq.h>
#include <asm/generic/devs.h>
#include <asm/generic/setup.h>
#include <asm/generic/timer.h>
#include <asm/generic/util.h>

#include <l4/sys/icu.h>
#include <l4/sys/cache.h>

static void __init l4x_mach_map_io(void)
{
}

static void __init l4x_mach_fixup(struct tag *tags, char **cmdline)
{
	*cmdline = boot_command_line;
}

#ifdef CONFIG_DEBUG_LL
#include <l4/sys/kdebug.h>
void printascii(const char *buf)
{
	outstring(buf);
}
#endif

static void init_irq(unsigned int irq, struct irq_chip *chip,
                          l4_cap_idx_t icu)
{
	irq_set_chip_and_handler(irq, chip, handle_level_irq);
	set_irq_flags(irq, IRQF_VALID);
	l4x_alloc_irq_desc_data(irq, icu);
}

#ifdef CONFIG_OF
static int l4x_of_found_l4icu;

static int domain_map(struct irq_domain *d, unsigned int irq,
                      irq_hw_number_t hw)
{
	init_irq(irq, &l4x_irq_icu_chip, (l4_cap_idx_t)d->host_data);
	return 0;
}

static int domain_xlate(struct irq_domain *d,
                        struct device_node *controller,
                        const u32 *intspec, unsigned int intsize,
                        unsigned long *out_hwirq,
                        unsigned int *out_type)
{
	if (d->of_node != controller)
		return -EINVAL;

	if (intsize < 3)
		return -EINVAL;

	*out_hwirq = intspec[1] + 32;
	*out_type  = intspec[2] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static const struct irq_domain_ops l4x_irq_domain_ops = {
	.map   = domain_map,
	.xlate = domain_xlate,
};

int __init irq_l4icu_init(struct device_node *node,
                          struct device_node *parent)
{
	int irq_base;
	l4_icu_info_t icu_info;
	l4_cap_idx_t icu = L4_INVALID_CAP;
	const char *capname;
	int r;
	struct irq_domain *domain;

	if (!of_property_read_string(node, "cap", &capname))
		if (l4x_re_resolve_name(capname, &icu)) {
			pr_err("l4x: ICU cap '%s' not found.\n",
			        capname);
			icu = L4_INVALID_CAP;
		}

	if (l4_is_invalid_cap(icu))
		icu = l4io_request_icu();

	if (l4_is_invalid_cap(icu)) {
		pr_err("l4x: Invalid L4ICU cap.\n");
		return -ENOENT;
	}

	r = l4_error(l4_icu_info(icu, &icu_info));
	if (r) {
		pr_err("l4x: Cannot query L4ICU: %d\n", r);
		return -ENOENT;
	}

	pr_info("l4icu: %u IRQs\n", icu_info.nr_irqs);

	BUG_ON(L4X_IRQS_V_DYN_BASE < icu_info.nr_irqs);

	irq_base = irq_alloc_descs(0, 0, icu_info.nr_irqs, numa_node_id());
	if (IS_ERR_VALUE(irq_base)) {
		irq_base = 0;
	}

	domain = irq_domain_add_legacy(node, icu_info.nr_irqs,
	                               irq_base, 0, &l4x_irq_domain_ops,
	                               (void *)icu);
	if (!domain)
		pr_err("l4icu: Domain creation failed\n");

	l4x_of_found_l4icu = 1;

	return 0;
}

/* This needs to be in the boards matches table in case there are other
   irq-controllers too */
static const struct of_device_id irq_matches[] = {
	{
		.compatible = "l4,icu",
		.data       = irq_l4icu_init,
	},
	{}
};

static void __init l4x_mach_init_of_irq(void)
{
	if (1)
		of_irq_init(irq_matches);
}
#endif

static void __init l4x_mach_init_irq(void)
{
	int i = 0;

	/* Call our generic IRQ handling code */
	l4lx_irq_init();

#ifdef CONFIG_OF
	l4x_mach_init_of_irq();

	if (l4x_of_found_l4icu)
		i = L4X_IRQS_V_DYN_BASE;
#endif

	for (; i < L4X_IRQS_V_DYN_BASE; i++)
		init_irq(i, &l4x_irq_icu_chip, l4io_request_icu());
	for (; i < L4X_IRQS_V_STATIC_BASE; ++i)
		init_irq(i, &l4x_irq_plain_chip, L4_INVALID_CAP);
}

#ifdef CONFIG_L4_CLK_NOOP
int clk_enable(struct clk *clk)
{
	printk("%s %d\n", __func__, __LINE__);
        return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	printk("%s %d\n", __func__, __LINE__);
}
EXPORT_SYMBOL(clk_disable);
#endif

int dma_needs_bounce(struct device *d, dma_addr_t a, size_t s)
{
	return 1;
}


#ifdef CONFIG_OUTER_CACHE
int __init l2x0_of_init(u32 aux_val, u32 aux_mask)
{
	return 0;
}

static void l4x_outer_cache_inv_range(unsigned long start, unsigned long end)
{
	l4_cache_l2_inv((unsigned long)phys_to_virt(start),
	                (unsigned long)phys_to_virt(end));
}

static void l4x_outer_cache_clean_range(unsigned long start, unsigned long end)
{
	l4_cache_l2_clean((unsigned long)phys_to_virt(start),
	                  (unsigned long)phys_to_virt(end));
}

static void l4x_outer_cache_flush_range(unsigned long start, unsigned long end)
{
	l4_cache_l2_flush((unsigned long)phys_to_virt(start),
	                  (unsigned long)phys_to_virt(end));
}

static void l4x_outer_cache_flush_all(void)
{
	printk("%s called by %p\n", __func__, __builtin_return_address(0));
}

static void l4x_outer_cache_disable(void)
{
	printk("%s called by %p\n", __func__, __builtin_return_address(0));
}

static void __init l4x_setup_outer_cache(void)
{
	outer_cache.inv_range   = l4x_outer_cache_inv_range;
	outer_cache.clean_range = l4x_outer_cache_clean_range;
	outer_cache.flush_range = l4x_outer_cache_flush_range;
	outer_cache.flush_all   = l4x_outer_cache_flush_all;
	outer_cache.disable     = l4x_outer_cache_disable;
}
#endif

static struct {
	struct tag_header hdr1;
	struct tag_core   core;
	struct tag_header hdr2;
	struct tag_mem32  mem;
	struct tag_header hdr3;
} l4x_atag __initdata = {
	{ tag_size(tag_core), ATAG_CORE },
	{ 1, PAGE_SIZE, 0xff },
	{ tag_size(tag_mem32), ATAG_MEM },
	{ 0 },
	{ 0, ATAG_NONE }
};

static void __init l4x_mach_init(void)
{
	l4x_atag.mem.start = PAGE_SIZE;
	l4x_atag.mem.size  = 0xbf000000 - PAGE_SIZE;

#ifdef CONFIG_OUTER_CACHE
	l4x_setup_outer_cache();
#endif

	l4x_arm_devices_init();
}

static inline void l4x_stop(enum reboot_mode mode, const char *cmd)
{
	local_irq_disable();
	l4x_exit_l4linux();
}

static struct delay_timer arch_delay_timer;

static unsigned long read_cntvct(void)
{
	unsigned long long v;
	asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (v));
	return v;
}

static unsigned long read_cntfrq(void)
{
	unsigned long v;
	asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (v));
	return v;
}

static void setup_delay_counter(void)
{
	arch_delay_timer.read_current_timer = read_cntvct;
	arch_delay_timer.freq = read_cntfrq();
	BUG_ON(arch_delay_timer.freq == 0);
	register_current_timer_delay(&arch_delay_timer);
}

static void __init l4x_arm_timer_init(void)
{
	l4x_timer_init();

	if (0)
		setup_delay_counter();
}

extern struct smp_operations l4x_smp_ops;

MACHINE_START(L4, "L4")
	.atag_offset    = (unsigned long)&l4x_atag - PAGE_OFFSET,
	.smp		= smp_ops(l4x_smp_ops),
	.fixup		= l4x_mach_fixup,
	.map_io		= l4x_mach_map_io,
	.init_irq	= l4x_mach_init_irq,
	.init_time	= l4x_arm_timer_init,
	.init_machine	= l4x_mach_init,
	.restart	= l4x_stop,
MACHINE_END

static char const *l4x_dt_compat[] __initdata = {
	"L4Linux",
	NULL
};

DT_MACHINE_START(L4_DT, "L4Linux (DT)")
	.smp		= smp_ops(l4x_smp_ops),
	.map_io		= l4x_mach_map_io,
	.init_machine	= l4x_mach_init,
	.init_late	= NULL,
	.init_irq	= l4x_mach_init_irq,
	.init_time	= l4x_arm_timer_init,
	.dt_compat	= l4x_dt_compat,
	.restart	= l4x_stop,
MACHINE_END

/*
 * We only have one machine description for now, so keep lookup_machine_type
 * simple.
 */
const struct machine_desc *lookup_machine_type(unsigned int x)
{
	return &__mach_desc_L4;
}

#ifdef CONFIG_SMP
#include <asm/generic/smp_ipi.h>

void l4x_raise_softirq(const struct cpumask *mask, unsigned ipi)
{
	int cpu;

	for_each_cpu(cpu, mask) {
		l4x_cpu_ipi_enqueue_vector(cpu, ipi);
		l4x_cpu_ipi_trigger(cpu);
	}
}
#endif
