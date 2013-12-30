
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/irq.h>

#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/generic/irq.h>
#include <asm/l4lxapi/irq.h>

#include <l4/sys/types.h>
#include <l4/sys/icu.h>
#include <l4/io/io.h>


static l4_cap_idx_t caps[L4X_NR_IRQS_V_DYN];
static int init_done;
static DEFINE_SPINLOCK(lock);

static void init_array(void)
{
	int i;

	BUILD_BUG_ON(L4X_NR_IRQS_V_DYN < 1);

	for (i = 0; i < L4X_NR_IRQS_V_DYN; ++i)
		caps[i] = L4_INVALID_CAP;

	init_done = 1;
}


int l4x_register_irq(l4_cap_idx_t irqcap)
{
	unsigned long flags;
	int i, ret = -1;

	if (!init_done)
		init_array();

	spin_lock_irqsave(&lock, flags);

	for (i = 0; i < L4X_NR_IRQS_V_DYN; ++i) {
		if (l4_is_invalid_cap(caps[i])) {
			caps[i] = irqcap;
			ret = i + L4X_IRQS_V_DYN_BASE;
			break;
		}
	}
	spin_unlock_irqrestore(&lock, flags);

	return ret;
}

void l4x_unregister_irq(int irqnum)
{
	if (irqnum >= L4X_IRQS_V_DYN_BASE
	    && (irqnum - L4X_IRQS_V_DYN_BASE) < L4X_NR_IRQS_V_DYN)
		caps[irqnum - L4X_IRQS_V_DYN_BASE] = L4_INVALID_CAP;
}

l4_cap_idx_t l4x_have_irqcap(int irqnum)
{
	if (!init_done)
		init_array();

	if (irqnum >= L4X_IRQS_V_DYN_BASE
	    && (irqnum - L4X_IRQS_V_DYN_BASE) < L4X_NR_IRQS_V_DYN)
		return caps[irqnum - L4X_IRQS_V_DYN_BASE];

	return L4_INVALID_CAP;
}

int l4lx_irq_set_wake(struct irq_data *data, unsigned int on)
{
	unsigned m = on ? L4_IRQ_F_SET_WAKEUP : L4_IRQ_F_CLEAR_WAKEUP;

	if (L4XV_FN_i(l4_error(l4_icu_set_mode(l4io_request_icu(),
	                                       data->irq, m))) < 0) {
		pr_err("l4x-irq: Failed to set wakeup type for IRQ %d\n",
		       data->irq);
		return -EINVAL;
	}

	return 0;
}

struct irq_chip l4x_irq_io_chip = {
	.name                   = "L4-io",
	.irq_startup            = l4lx_irq_io_startup,
	.irq_shutdown           = l4lx_irq_io_shutdown,
	.irq_enable             = l4lx_irq_dev_enable,
	.irq_disable            = l4lx_irq_dev_disable,
	.irq_ack                = l4lx_irq_dev_ack,
	.irq_mask               = l4lx_irq_dev_mask,
	.irq_mask_ack           = l4lx_irq_dev_mask_ack,
	.irq_unmask             = l4lx_irq_dev_unmask,
	.irq_eoi                = l4lx_irq_dev_eoi,
	.irq_set_type           = l4lx_irq_set_type,
	.irq_set_wake           = l4lx_irq_set_wake,
#ifdef CONFIG_L4_VCPU
#ifdef CONFIG_SMP
	.irq_set_affinity       = l4lx_irq_dev_set_affinity,
#endif
#endif
};

struct irq_chip l4x_irq_plain_chip = {
	.name                   = "L4-plain",
	.irq_startup            = l4lx_irq_plain_startup,
	.irq_shutdown           = l4lx_irq_plain_shutdown,
	.irq_enable             = l4lx_irq_dev_enable,
	.irq_disable            = l4lx_irq_dev_disable,
	.irq_ack                = l4lx_irq_dev_ack,
	.irq_mask               = l4lx_irq_dev_mask,
	.irq_mask_ack           = l4lx_irq_dev_mask_ack,
	.irq_unmask             = l4lx_irq_dev_unmask,
	.irq_eoi                = l4lx_irq_dev_eoi,
	.irq_set_type           = l4lx_irq_set_type,
	.irq_set_wake           = l4lx_irq_set_wake,
#ifdef CONFIG_L4_VCPU
#ifdef CONFIG_SMP
	.irq_set_affinity       = l4lx_irq_dev_set_affinity,
#endif
#endif
};

#if defined(CONFIG_X86) && defined(CONFIG_SMP)
#include <linux/interrupt.h>
#include <linux/sched.h>

void l4x_smp_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *oldregs;
	unsigned long flags;
	oldregs = set_irq_regs(regs);

	local_irq_save(flags);
	irq_enter();
	profile_tick(CPU_PROFILING);
	update_process_times(user_mode_vm(get_irq_regs()));
	irq_exit();
	local_irq_restore(flags);
	set_irq_regs(oldregs);
}
#endif
