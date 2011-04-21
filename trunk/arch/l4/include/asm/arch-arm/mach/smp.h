#ifndef ASMARM_ARCH_SMP_H
#define ASMARM_ARCH_SMP_H

#include <asm/generic/smp_ipi.h>

int l4x_cpu_cpu_get(void);

#define hard_smp_processor_id() (l4x_cpu_cpu_get())

/*
 * Send IPI.
 */
static inline void smp_cross_call(const struct cpumask *mask, int ipi)
{
	int cpu;

	for_each_cpu(cpu, mask) {
		l4x_cpu_ipi_enqueue_vector(cpu, ipi);
		l4x_cpu_ipi_trigger(cpu);
	}
}

#endif
