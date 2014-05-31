
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

union irqcap_elem_t {
	l4_cap_idx_t cap;
	l4_cap_idx_t __percpu *caps;
};

static union irqcap_elem_t caps[L4X_NR_IRQS_V_DYN];
static int init_done;
static DEFINE_SPINLOCK(lock);

enum { PERCPU_IRQ_FLAG = 1 };

static void init_array(void)
{
	int i;

	BUILD_BUG_ON(L4X_NR_IRQS_V_DYN < 1);

	for (i = 0; i < L4X_NR_IRQS_V_DYN; ++i)
		caps[i].cap = L4_INVALID_CAP;

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
		if (!(caps[i].cap & PERCPU_IRQ_FLAG)
		    && l4_is_invalid_cap(caps[i].cap)) {
			caps[i].cap = irqcap & L4_CAP_MASK;
			ret = i + L4X_IRQS_V_DYN_BASE;
			break;
		}
	}
	spin_unlock_irqrestore(&lock, flags);

	return ret;
}

static inline l4_cap_idx_t __percpu *ppcaps(int idx)
{
	return (l4_cap_idx_t __percpu *)((unsigned long)caps[idx].caps & ~PERCPU_IRQ_FLAG);
}

int l4x_alloc_percpu_irq(l4_cap_idx_t __percpu **percpu_caps)
{
	int irq = l4x_register_irq(L4_INVALID_CAP);

	if (irq != -1) {
		int i = irq - L4X_IRQS_V_DYN_BASE;
		struct l4x_irq_desc_private *p = irq_get_chip_data(irq);

		caps[i].caps = alloc_percpu(l4_cap_idx_t);
		caps[i].cap |= PERCPU_IRQ_FLAG;
		irq_set_percpu_devid(irq);
		irq_set_chip_and_handler(irq, &l4x_irq_plain_chip, handle_percpu_devid_irq);
		p->is_percpu = 1;
		p->c.irq_caps = ppcaps(i);
		*percpu_caps = p->c.irq_caps;
	}

	return irq;
}

void l4x_free_percpu_irq(int irq)
{
	int i = irq - L4X_IRQS_V_DYN_BASE;

	BUG_ON(irq < L4X_IRQS_V_DYN_BASE);
	BUG_ON(i >= L4X_NR_IRQS_V_DYN);

	free_percpu(ppcaps(i));
	caps[i].cap = L4_INVALID_CAP;
	l4x_unregister_irq(irq);
}

int l4x_register_percpu_irqcap(int irqnum, unsigned cpu, l4_cap_idx_t cap)
{
	int i = irqnum - L4X_IRQS_V_DYN_BASE;

	if (irqnum >= L4X_IRQS_V_DYN_BASE && i < L4X_NR_IRQS_V_DYN) {
		*per_cpu_ptr(ppcaps(i), cpu) = cap;
		return 0;
	}

	return -EINVAL;
}

void l4x_unregister_irq(int irqnum)
{
	int i = irqnum - L4X_IRQS_V_DYN_BASE;
	if (irqnum >= L4X_IRQS_V_DYN_BASE && i < L4X_NR_IRQS_V_DYN) {
		if (caps[i].cap & PERCPU_IRQ_FLAG) {
			caps[i].cap &= ~PERCPU_IRQ_FLAG;
			free_percpu(caps[i].caps);
		} else
			caps[i].cap = L4_INVALID_CAP;
	}
}

l4_cap_idx_t l4x_have_irqcap(int irqnum, unsigned cpu)
{
	int i = irqnum - L4X_IRQS_V_DYN_BASE;

	if (!init_done)
		init_array();

	if (cpu >= NR_CPUS)
		return L4_INVALID_CAP;

	if (irqnum >= L4X_IRQS_V_DYN_BASE && i < L4X_NR_IRQS_V_DYN) {
		if (caps[i].cap & PERCPU_IRQ_FLAG)
			return *per_cpu_ptr(ppcaps(i), cpu);
		return caps[i].cap;
	}

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
