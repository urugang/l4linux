/*
 * IRQ implementation using Omega0.
 */

#include <asm/types.h>
#include <asm/api/config.h>

#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>

#include <asm/l4lxapi/irq.h>
#include <asm/l4lxapi/thread.h>
#include <asm/l4lxapi/misc.h>

#include <asm/generic/sched.h>
#include <asm/generic/setup.h>
#include <asm/generic/task.h>
#include <asm/generic/do_irq.h>
#include <asm/generic/l4lib.h>

#include <l4/log/l4log.h>
#include <l4/sys/kdebug.h>
#include <l4/omega0/client.h>

#ifdef CONFIG_L4_USE_L4VMM
#include <l4/vmm/vmm-compat.h>
#endif

L4_EXTERNAL_FUNC(omega0_attach);
L4_EXTERNAL_FUNC(omega0_request);
L4_EXTERNAL_FUNC(omega0_detach);

/* bitmap containing '1' if the corresponding irq was requested from
 *  * Omega0 but not yet unmasked. */
static unsigned long irq_masked_at_omega0;

/* Bitmask of omega0 IRQs */
static unsigned long omega0_irqs;

/* This is a copy of the prios in l4lxlib/V2/irq.c
 * XXX: Join this in a sane way! */
static char irq_prio[NR_IRQS] =
   /*  0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
     { 1, 0,15, 6, 5, 4, 3, 2,14,13,12,11,10, 9, 8, 7};

static int irq_handle[NR_IRQS];
static l4_threadid_t detach_thread;

DEFINE_SPINLOCK(l4_irq_lock);

static void l4lx_irq_omega0_detach_call(void)
{
	int i, e;
	l4_threadid_t me = l4_myself();
	l4_msgdope_t dope;

	for (i = 0; i < NR_IRQS; i++) {
		if (l4_thread_equal(me, irq_id[i])) {
			omega0_irqdesc_t desc;

			desc.s.num = i + 1;
			if (omega0_detach(desc))
				LOG_printf("Error detaching IRQ %d\n", i);

			e = l4_ipc_send(detach_thread, L4_IPC_SHORT_MSG, 0, 0,
			                L4_IPC_NEVER, &dope);
			if (e)
				LOG_printf("Error sending IPC: %x\n", e);
		}
	}

	l4_sleep_forever();
}

static void l4lx_irq_omega0_detach(void)
{
	int i, e;
	l4_threadid_t inv_id = L4_INVALID_ID;
	l4_msgdope_t dope;
	l4_umword_t d;

	detach_thread = l4_myself();

	for (i = 0; i < NR_IRQS; i++) {
		if (!test_bit(i, &omega0_irqs) || l4_is_nil_id(irq_id[i]))
			continue;

		l4_thread_ex_regs(irq_id[i],
		                  (l4_umword_t)l4lx_irq_omega0_detach_call,
		                  ~0UL, &inv_id, &inv_id, &d, &d, &d);

		e = l4_ipc_receive(irq_id[i], L4_IPC_SHORT_MSG, &d, &d,
		                   L4_IPC_NEVER, &dope);
		if (e)
			LOG_printf("Error receiving IPC: %x\n", e);
	}
}

void l4lx_irq_init(void)
{
	l4lx_irq_max = NR_IRQS;
	atexit(l4lx_irq_omega0_detach);
}

int l4lx_irq_prio_get(unsigned int irq)
{
	if (irq < NR_IRQS)
		return CONFIG_L4_PRIO_IRQ_BASE + irq_prio[irq];

	enter_kdebug("l4lx_irq_prio_get: wrong IRQ!");
	return -1;
}

/*
 * Acquire an IRQ from the Omega0 server.
 * Return: >0 on success, 0 on failure
 */
static int acquire_irq(unsigned int irq)
{
	/* Try to get the interrupt IRQ from Omega0. We know that Omega0
	 * gets IDs, which are calculated as id=irq+1.  We assume we use
	 * shared interrupts.
	 */
	omega0_irqdesc_t desc;

	desc.s.shared = 1;
	desc.s.num = irq + 1;

	if ((irq_handle[irq] = omega0_attach(desc)) < 0) {
		/* failure to attach to IRQ */
		LOG_printf("%s: Error attaching to IRQ %d\n", __func__, irq);
		return 0;
	}

	set_bit(irq, &irq_masked_at_omega0);
	set_bit(irq, &omega0_irqs);

	return 1;
}

static inline void wait_for_irq_message(unsigned int irq)
{
	  /* We do not explicitely consume an interrupt, because we do not
	   * know if it was actually for us. We also rely on the auto-mask
	   * feature of Omega0, and do not mask/unmask the interrupts
	   * ourself.
	   *
	   * As a consequence, others wont get interrupts as long as we do
	   * not wait for the next interrupt.
	   */
	omega0_request_t request;
	int err;

	for (;;) {
		if (test_and_clear_bit(irq, &irq_masked_at_omega0))
			request = OMEGA0_RQ(OMEGA0_WAIT | OMEGA0_UNMASK, irq + 1);
		else
			request = OMEGA0_RQ(OMEGA0_WAIT, irq + 1);

		if ((err = omega0_request(irq_handle[irq], request)) >= 0)
			break;

		LOG_printf("%s: irq %u receive failed, code = 0x%x\n",
		           __func__, irq, (unsigned) err);
	}
}

static L4_CV void irq_dev_thread(void *data)
{
	unsigned int irq = *(unsigned int *)data;
	struct thread_info *ctx = current_thread_info();

	l4x_prepare_irq_thread(ctx, 0);

	/* Get the IRQ from Omega0 */
	if (!acquire_irq(irq)) {
		/* "reset" chip ... */
		irq_desc[irq].chip = &no_irq_type;

		/* complain */
		/* XXX: actually we have to get the IRQ in the
		 * startup function! */
		l4lx_sleep_forever();
	}

	for (;;) {
		wait_for_irq_message(irq);
		l4x_do_IRQ(irq, ctx);
	}
}

unsigned int l4lx_irq_dev_startup_hw(unsigned int irq)
{
	char thread_name[7];

	/* first time? */
	if (!test_and_set_bit(irq, irq_threads_started)) {
		LOG_printf("%s: Starting IRQ thread for IRQ %d.\n",
		           __func__, irq);

		/* Create IRQ thread */
		sprintf(thread_name, "IRQ%d", irq);
		irq_id[irq] = l4lx_thread_create(irq_dev_thread,
		                                 smp_processor_id(),
						 NULL,
						 &irq, sizeof(irq),
						 l4lx_irq_prio_get(irq),
						 thread_name);
	}

	return 1;
}

void l4lx_irq_dev_shutdown_hw(unsigned int irq)
{
	l4lx_irq_dev_disable_hw(irq);
}

void l4lx_irq_dev_enable_hw(unsigned int irq)
{}

void l4lx_irq_dev_disable_hw(unsigned int irq)
{}

void l4lx_irq_dev_ack_hw(unsigned int irq)
{}

void l4lx_irq_dev_mask_hw(unsigned int irq)
{}

void l4lx_irq_dev_unmask_hw(unsigned int irq)
{}

void l4lx_irq_dev_end_hw(unsigned int irq)
{}

/*
 * _virt functions are the same as the _hw functions when using omega0
 */
unsigned int l4lx_irq_dev_startup_virt(unsigned int irq)
{ return l4lx_irq_dev_startup_hw(irq); }
void l4lx_irq_dev_shutdown_virt(unsigned int irq)
{ l4lx_irq_dev_shutdown_hw(irq); }
void l4lx_irq_dev_ack_virt(unsigned int irq)
{ l4lx_irq_dev_ack_hw(irq); }
void l4lx_irq_dev_mask_virt(unsigned int irq)
{ l4lx_irq_dev_mask_hw(irq); }
void l4lx_irq_dev_unmask_virt(unsigned int irq)
{ l4lx_irq_dev_unmask_hw(irq); }
void l4lx_irq_dev_end_virt(unsigned int irq)
{ l4lx_irq_dev_end_hw(irq); }
void l4lx_irq_dev_enable_virt(unsigned int irq)
{ l4lx_irq_dev_enable_hw(irq); }
void l4lx_irq_dev_disable_virt(unsigned int irq)
{ l4lx_irq_dev_disable_hw(irq); }
