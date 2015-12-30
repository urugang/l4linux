#ifndef _ASM_X86_MMU_H
#define _ASM_X86_MMU_H

#include <linux/spinlock.h>
#include <linux/mutex.h>

#ifdef CONFIG_L4
#include <asm/generic/mmu.h>
#endif /* L4 */

/*
 * The x86 doesn't have a mmu context, but
 * we put the segment information here.
 */
typedef struct {
#ifdef CONFIG_MODIFY_LDT_SYSCALL
	struct ldt_struct *ldt;
#endif

#ifdef CONFIG_X86_64
	/* True if mm supports a task running in 32 bit compatibility mode. */
	unsigned short ia32_compat;
#endif

	struct mutex lock;
	void __user *vdso;

	atomic_t perf_rdpmc_allowed;	/* nonzero if rdpmc is allowed */
#ifdef CONFIG_L4
	l4_cap_idx_t task;
	enum l4x_unmap_mode_enum l4x_unmap_mode;
#endif /* L4 */
} mm_context_t;

#ifdef CONFIG_SMP
void leave_mm(int cpu);
#else
static inline void leave_mm(int cpu)
{
}
#endif

#ifdef CONFIG_L4
#define __HAVE_ARCH_ENTER_LAZY_MMU_MODE
static inline void arch_enter_lazy_mmu_mode(void) {}
static inline void arch_leave_lazy_mmu_mode(void) { l4x_unmap_log_flush(); }
static inline void arch_flush_lazy_mmu_mode(void) { l4x_unmap_log_flush(); }
#endif /* L4 */

#endif /* _ASM_X86_MMU_H */
