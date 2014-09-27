#ifndef __ASM_L4__GENERIC__IRQ_H__
#define __ASM_L4__GENERIC__IRQ_H__

#include <asm/generic/kthreads.h>

#ifdef CONFIG_L4_VCPU
#define L4X_VCPU_IRQ_IPI (NR_IRQS)
#else
#define L4_IRQ_DISABLED 0
#define L4_IRQ_ENABLED  1
#endif

int l4x_register_irq(l4_cap_idx_t irqcap);
int l4x_alloc_percpu_irq(l4_cap_idx_t __percpu **percpucaps);
void l4x_free_percpu_irq(int irq);
int l4x_register_percpu_irqcap(int irq, unsigned cpu, l4_cap_idx_t cap);
void l4x_unregister_irq(int irqnum);
l4_cap_idx_t l4x_have_irqcap(int irqnum, unsigned cpu);

struct l4x_irq_desc_private {
	unsigned is_percpu;
	union {
		l4_cap_idx_t irq_cap;
		l4_cap_idx_t __percpu *irq_caps;
	} c;
	l4_cap_idx_t icu;
#ifndef CONFIG_L4_VCPU
	l4lx_thread_t irq_thread;
#endif
	unsigned enabled;
	unsigned cpu;
	unsigned char trigger;
};

int l4x_alloc_irq_desc_data(int irq, l4_cap_idx_t icu);
l4_cap_idx_t l4x_irq_init(l4_cap_idx_t icu, unsigned irq, unsigned trigger,
                          char *tag);
void l4x_irq_release(l4_cap_idx_t irqcap);

void l4x_init_IRQ(void);

extern struct irq_chip l4x_irq_icu_chip;
extern struct irq_chip l4x_irq_plain_chip;

#endif /* ! __ASM_L4__GENERIC__IRQ_H__ */
