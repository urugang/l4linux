#ifndef ASMARM_ARCH_SMP_H
#define ASMARM_ARCH_SMP_H

int l4x_cpu_cpu_get(void);
void l4x_cpu_ipi_trigger(unsigned cpu);

void l4x_smp_broadcast_timer(void);

#define hard_smp_processor_id() (l4x_cpu_cpu_get())

/*
 * Send IPI.
 */
static inline void smp_cross_call(const struct cpumask *mask)
{
	int cpu;

	for_each_cpu(cpu, mask) {
		l4x_cpu_ipi_trigger(cpu);
	}
}

#endif
