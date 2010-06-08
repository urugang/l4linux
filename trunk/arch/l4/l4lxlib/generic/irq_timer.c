/*
 * This file implements the timer interrupt in a generic
 * manner. The interface is defined in asm-l4/l4lxapi/irq.h.
 */

#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/generic/dispatch.h>
#include <asm/generic/irq.h>
#include <asm/generic/sched.h>
#include <asm/generic/setup.h>
#include <asm/generic/task.h>
#include <asm/generic/do_irq.h>
#include <asm/generic/suspres.h>
#include <asm/generic/smp.h>
#include <asm/generic/cap_alloc.h>
#include <asm/generic/stack_id.h>

#include <asm/l4lxapi/irq.h>
#include <asm/l4lxapi/thread.h>
#include <asm/l4lxapi/misc.h>

#include <l4/sys/irq.h>
#include <l4/sys/factory.h>
#include <l4/log/log.h>
#include <l4/re/env.h>

/*
 * Timer interrupt thread.
 */
void L4_CV timer_irq_thread(void *data)
{
	l4_timeout_t to;
	l4_kernel_clock_t pint;
	struct thread_info *ctx = current_thread_info();
	l4_utcb_t *u = l4_utcb();
	unsigned cpu = *(unsigned *)data;

	l4x_prepare_irq_thread(ctx, cpu);

	printk("%s: Starting timer IRQ thread.\n", __func__);

	pint = l4lx_kinfo->clock;
	for (;;) {
		pint += 1000000 / HZ;

		if (pint > l4lx_kinfo->clock) {
			l4_rcv_timeout(l4_timeout_abs_u(pint, 1, u), &to);
			l4_ipc_receive(L4_INVALID_CAP, u, to);
		}

		l4x_do_IRQ(TIMER_IRQ, ctx);
		l4x_smp_broadcast_timer();
	}
} /* timer_irq_thread */

static void deep_sleep(void)
{
	l4_sleep_forever();
}

static void suspend_resume_func(enum l4x_suspend_resume_state state)
{
	struct l4x_irq_desc_private *p = get_irq_chip_data(TIMER_IRQ);
	switch (state) {
		case L4X_SUSPEND:
			l4x_thread_set_pc(p->irq_thread, deep_sleep);
			break;

		case L4X_RESUME:
			l4x_thread_set_pc(p->irq_thread, timer_irq_thread);
			break;
	};
}

int l4lx_timer_started;

/*
 * public functions.
 */
unsigned int l4lx_irq_timer_startup(unsigned int irq)
{
	char thread_name[15];
	int cpu = smp_processor_id();
	struct l4x_irq_desc_private *p = get_irq_chip_data(irq);
	static struct l4x_suspend_resume_struct susp_res;

	printk("%s(%d)\n", __func__, irq);

	if (p->enabled)
		return 0;

	BUG_ON(TIMER_IRQ != irq);

	sprintf(thread_name, "timer.i%d", irq);

	l4x_suspend_resume_register(suspend_resume_func, &susp_res);

	p->irq_thread = l4lx_thread_create
			(timer_irq_thread,	/* thread function */
	                 cpu,                   /* cpu */
			 NULL,			/* stack */
			 &cpu, sizeof(cpu),	/* data */
			 l4lx_irq_prio_get(irq),/* prio */
			 0,                     /* flags */
			 thread_name);		/* ID */

	if (l4_is_invalid_cap(p->irq_thread))
		enter_kdebug("Error creating timer thread!");

	return 1;
}

#ifdef CONFIG_SMP
int l4lx_irq_timer_set_affinity(unsigned int irq, const struct cpumask *dest)
{
	// timer should always be on cpu0 currently
	return 0;
}
#endif

void l4lx_irq_timer_shutdown(unsigned int irq)
{}

void l4lx_irq_timer_enable(unsigned int irq)
{}

void l4lx_irq_timer_disable(unsigned int irq)
{}

void l4lx_irq_timer_ack(unsigned int irq)
{}

void l4lx_irq_timer_end(unsigned int irq)
{}

void l4lx_irq_timer_mask(unsigned int irq)
{}

void l4lx_irq_timer_unmask(unsigned int irq)
{}
