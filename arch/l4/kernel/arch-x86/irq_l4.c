/* linux/arch/l4/kernel/arch-i386/irq_l4.c */

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <asm/l4lxapi/irq.h>

#include <asm/generic/irq.h>
#include <asm/generic/task.h>
#include <asm/generic/stack_id.h>

#ifdef CONFIG_L4_IRQ_SINGLE
struct irq_chip l4x_irq_timer_chip = {
	.name		= "L4-timer",
	.startup	= l4lx_irq_timer_startup,
	.shutdown	= l4lx_irq_timer_shutdown,
	.enable		= l4lx_irq_timer_enable,
	.disable	= l4lx_irq_timer_disable,
	.ack		= l4lx_irq_timer_ack,
	.mask		= l4lx_irq_timer_mask,
	.unmask		= l4lx_irq_timer_unmask,
	.end		= l4lx_irq_timer_end,
	.eoi		= l4lx_irq_timer_end,
#ifndef CONFIG_L4_VCPU
#ifdef CONFIG_SMP
	.set_affinity	= l4lx_irq_timer_set_affinity,
#endif
#endif
};
#endif

struct irq_chip l4x_irq_dev_chip = {
	.name		= "L4-irq",
	.startup	= l4lx_irq_dev_startup,
	.shutdown	= l4lx_irq_dev_shutdown,
	.enable		= l4lx_irq_dev_enable,
	.disable	= l4lx_irq_dev_disable,
	.ack		= l4lx_irq_dev_ack,
	.mask		= l4lx_irq_dev_mask,
	.unmask		= l4lx_irq_dev_unmask,
	.end		= l4lx_irq_dev_end,
	.eoi		= l4lx_irq_dev_eoi,
	.set_type	= l4lx_irq_set_type,
#ifdef CONFIG_L4_VCPU
#ifdef CONFIG_SMP
	.set_affinity	= l4lx_irq_dev_set_affinity,
#endif
#endif
};

union irq_ctx {
	struct thread_info	tinfo;
	u32			stack[THREAD_SIZE/sizeof(u32)];
};

static union irq_ctx *softirq_ctx;

static char softirq_stack[THREAD_SIZE]
		__attribute__((__aligned__(THREAD_SIZE)));

static void l4x_init_softirq_stack(void)
{
	softirq_ctx = (union irq_ctx *)softirq_stack;
	softirq_ctx->tinfo.task			= NULL;
	softirq_ctx->tinfo.exec_domain		= NULL;
	softirq_ctx->tinfo.cpu			= 0;
	softirq_ctx->tinfo.preempt_count	= SOFTIRQ_OFFSET;
	softirq_ctx->tinfo.addr_limit		= MAKE_MM_SEG(0);
}

#ifdef CONFIG_HOTPLUG_CPU
void irq_force_complete_move(int irq)
{
	// tbd?
}
#endif

void __init l4x_init_IRQ(void)
{
	int i;

	l4lx_irq_init();
	l4x_init_softirq_stack();

	l4x_alloc_irq_desc_data(0);

#ifdef CONFIG_L4_IRQ_SINGLE
	set_irq_chip_and_handler_name(0, &l4x_irq_timer_chip, handle_edge_irq, "timer");
#else
	set_irq_chip_and_handler_name(0, &l4x_irq_dev_chip, handle_edge_irq, "edge");
#endif

	for (i = 1; i < NR_IRQS; i++) {
		if (i < l4lx_irq_max) {
			l4x_alloc_irq_desc_data(i);
			//set_irq_chip_and_handler_name(i, &l4x_irq_dev_chip, handle_edge_irq, "edge");
			set_irq_chip_and_handler_name(i, &l4x_irq_dev_chip, handle_fasteoi_irq, "fasteoi");
		} else
			set_irq_chip_and_handler(i, &no_irq_chip, handle_edge_irq);
	}
}
