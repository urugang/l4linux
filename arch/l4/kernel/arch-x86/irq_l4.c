#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/uaccess.h>

#include <asm/l4lxapi/irq.h>

#include <asm/generic/irq.h>
#include <asm/generic/task.h>
#include <asm/generic/stack_id.h>

#include <l4/io/io.h>

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

	for (i = 0; i < L4X_IRQS_V_DYN_BASE; i++) {
		l4x_alloc_irq_desc_data(i, l4io_request_icu());
		irq_set_chip_and_handler(i, &l4x_irq_icu_chip, handle_edge_eoi_irq);
	}
	for (i = L4X_IRQS_V_DYN_BASE; i < L4X_IRQS_V_STATIC_BASE; i++) {
		l4x_alloc_irq_desc_data(i, L4_INVALID_CAP);
		irq_set_chip_and_handler(i, &l4x_irq_plain_chip, handle_edge_eoi_irq);
	}

	/* from native_init_IRQ() */
#ifdef CONFIG_X86_32
	irq_ctx_init(smp_processor_id());
#endif
}
