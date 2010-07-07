#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/irq.h>

#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#include <asm/l4lxapi/irq.h>
#include <asm/api/config.h>
#include <asm/generic/irq.h>
#include <asm/generic/devs.h>
#include <asm/generic/setup.h>

#include <l4/sys/cache.h>

static void __init map_io_l4(void)
{
}

static void __init fixup_l4(struct machine_desc *desc, struct tag *tags,
                            char **cmdline, struct meminfo *mi)
{
}


static void l4x_irq_ackmaskun_empty(unsigned int irq)
{
#ifdef CONFIG_L4_VCPU
	printk("l4x_irq_ackmaskun_empty: %d\n", irq);
#endif
}

static int l4x_irq_type_empty(unsigned int irq, unsigned int type)
{
	return 0;
}

static int l4x_irq_wake_empty(unsigned int irq, unsigned int type)
{
	return 0;
}

static void l4x_irq_startup(unsigned int irq)
{
	l4lx_irq_dev_startup(irq);
}

static struct irq_chip l4_irq_dev_chip = {
	.name           = "L4",
	.ack            = l4x_irq_ackmaskun_empty,
	.mask           = l4lx_irq_dev_shutdown,
	.unmask         = l4x_irq_startup,
	.set_type       = l4x_irq_type_empty,
	.set_wake       = l4x_irq_wake_empty,
};

#if defined(CONFIG_L4_IRQ_SINGLE)
static struct irq_chip l4_irq_timer_chip = {
	.name           = "L4timer",
	.ack            = l4x_irq_ackmaskun_empty,
	.mask           = l4x_irq_ackmaskun_empty,
	.unmask         = l4x_irq_ackmaskun_empty,
	.set_type       = l4x_irq_type_empty,
	.set_wake       = l4x_irq_wake_empty,
};
#endif

void __init l4x_setup_irq(unsigned int irq)
{
	set_irq_chip           (irq, &l4_irq_dev_chip);
	set_irq_handler        (irq, handle_simple_irq);
	set_irq_flags          (irq, IRQF_VALID);
	l4x_alloc_irq_desc_data(irq);
}

static void __init init_irq_l4(void)
{
	int i;

	/* Call our generic IRQ handling code */
	l4lx_irq_init();

	for (i = 1; i < NR_IRQS; i++)
		l4x_setup_irq(i);
}

static void timer_set_mode(enum clock_event_mode mode,
                           struct clock_event_device *clk)
{
	// we only advertise periodic mode
}

static int timer_set_next_event(unsigned long evt,
                                struct clock_event_device *unused)
{
	printk("timer_set_next_event\n");
	return 0;
}


static struct clock_event_device timer0_clockevent = {
	.name           = "timer0",
	.shift          = 10,
	.features       = CLOCK_EVT_FEAT_PERIODIC,
	.set_mode       = timer_set_mode,
	.set_next_event = timer_set_next_event,
	.rating         = 300,
	.irq            = 0,
};

static irqreturn_t l4_timer_interrupt_handler(int irq, void *dev_id)
{
	timer0_clockevent.event_handler(&timer0_clockevent);
	return IRQ_HANDLED;
}

static struct irqaction timer_irq = {
	.name		= "L4 Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= l4_timer_interrupt_handler,
};

unsigned int do_IRQ(int irq, struct pt_regs *regs)
{
	extern asmlinkage void asm_do_IRQ(unsigned int irq, struct pt_regs *regs);
	asm_do_IRQ(irq, regs);
	return 0;
}

static cycle_t kip_read(struct clocksource *cs)
{
	return l4lx_kinfo->clock;
}

static struct clocksource clocksource_l4 =  {
	.name   = "kip",
	.rating = 300,
	.read   = kip_read,
	.mask   = CLOCKSOURCE_MASK(64),
	.shift  = 10,
	.flags  = CLOCK_SOURCE_IS_CONTINUOUS,
};

static void l4x_timer_init(void)
{
	l4x_alloc_irq_desc_data(0);

#if defined(CONFIG_L4_IRQ_SINGLE)
	set_irq_chip     (0, &l4_irq_timer_chip);
#else
	set_irq_chip     (0, &l4_irq_dev_chip);
#endif
	set_irq_handler  (0, handle_simple_irq);
	set_irq_flags    (0, IRQF_VALID);

	setup_irq(0, &timer_irq);

	clocksource_l4.mult =
		clocksource_khz2mult(1000, clocksource_l4.shift);
	clocksource_register(&clocksource_l4);


	timer0_clockevent.irq = 0;
	timer0_clockevent.mult =
		div_sc(1000000, NSEC_PER_SEC, timer0_clockevent.shift);
	timer0_clockevent.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &timer0_clockevent);
	timer0_clockevent.min_delta_ns =
		clockevent_delta2ns(0xf, &timer0_clockevent);
	timer0_clockevent.cpumask = cpumask_of(0);
	clockevents_register_device(&timer0_clockevent);

#if defined(CONFIG_L4_IRQ_SINGLE)
	l4lx_irq_timer_startup(0);
#endif
}

static void __init init_l4(void)
{
	l4x_arm_devices_init();
}

struct sys_timer l4x_timer = {
	.init		= l4x_timer_init,
};

MACHINE_START(L4, "L4")
	.phys_io	= 0,
	.io_pg_offst	= 0,
	.boot_params	= 0,
	.fixup		= fixup_l4,
	.map_io		= map_io_l4,
	.init_irq	= init_irq_l4,
	.timer		= &l4x_timer,
	.init_machine	= init_l4,
MACHINE_END

/*
 * We only have one machine description for now, so keep lookup_machine_type
 * simple.
 */
const struct machine_desc *lookup_machine_type(unsigned int x)
{
	return &__mach_desc_L4;
}



/* DMA functions */
void v4wb_dma_inv_range(const void *start, const void *end)
{
	l4_cache_inv_data((unsigned long)start, (unsigned long)end);
}

void v4wb_dma_clean_range(const void *start, const void *end)
{
	l4_cache_clean_data((unsigned long)start, (unsigned long)end);
}

void v4wb_dma_flush_range(const void *start, const void *end)
{
	l4_cache_flush_data((unsigned long)start, (unsigned long)end);
}
