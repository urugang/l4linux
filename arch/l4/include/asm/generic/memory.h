#ifndef __ASM_L4__GENERIC__MEMORY_H__
#define __ASM_L4__GENERIC__MEMORY_H__

#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/hardirq.h>

#include <asm/l4x/exception.h>

#include <l4/sys/types.h>

//#define DEBUG_PARSE_PTABS_READ 1
//#define DEBUG_PARSE_PTABS_WRITE 1

#ifdef CONFIG_ARM
#ifdef CONFIG_ARM_LPAE
#error Add me
#else
#define PF_EUSER		(1 << 14)
#define PF_EKERNEL		0
#define PF_EWRITE		(1 << 6)
#define PF_EREAD		0
#define PF_EPROTECTION		0xd
#define PF_ENOTPRESENT		0x8
#define L4X_PHYSICAL_PAGE_MASK PAGE_MASK
#endif
#else
#define PF_EUSER		4
#define PF_EKERNEL		0
#define PF_EWRITE		2
#define PF_EREAD		0
#define PF_EPROTECTION		1
#define PF_ENOTPRESENT		0

#define L4X_PHYSICAL_PAGE_MASK PHYSICAL_PAGE_MASK
#endif

#ifdef CONFIG_X86
#define PTE_VAL_FMTTYPE "l"
#endif
#ifdef CONFIG_ARM
#define PTE_VAL_FMTTYPE ""
#endif

extern void *l4x_main_memory_start;

enum { L4X_CHECK_IN_KERNEL_ACCESS = 0, };
int l4x_check_kern_region(void *address, unsigned long size, int rw);

int l4x_do_page_fault(unsigned long address, struct pt_regs *regs, unsigned long error_code);
#ifdef CONFIG_ARM
void do_DataAbort(unsigned long addr, unsigned int fsr, struct pt_regs *regs);
#endif

static inline pte_t *lookup_pte_lock(struct mm_struct *mm,
                                     unsigned long address,
                                     unsigned *page_shift,
                                     spinlock_t **ptl)
{
	pud_t *pud;
	pmd_t *pmd;
	pgd_t *pgd = mm->pgd + pgd_index(address);

	if (unlikely(!pgd_present(*pgd)))
		return NULL;

	pud = pud_offset(pgd, address);
	if (unlikely(!pud_present(*pud)))
		return NULL;

	pmd = pmd_offset(pud, address);
	if (unlikely(!pmd_present(*pmd)))
		return NULL;

	if (ptl)
		*ptl = pte_lockptr(mm, pmd);
#ifdef CONFIG_X86
	if (pmd_large(*pmd)) {
		*page_shift = PMD_SHIFT;
		return (pte_t *)pmd;
	}
#endif
	*page_shift = PAGE_SHIFT;
	return pte_offset_kernel(pmd, address);
}

static inline pte_t *lookup_pte(struct mm_struct *mm, unsigned long address,
                                unsigned *page_shift)
{
	return lookup_pte_lock(mm, address, page_shift, NULL);
}

static inline int l4x_pte_present_user(pte_t pte)
{
#ifdef CONFIG_X86
	return pte_present(pte) && (pte_val(pte) & _PAGE_USER);
#endif
#ifdef CONFIG_ARM
	return pte_present(pte) && (pte_val(pte) & L_PTE_USER);
#endif
	return 0;
}

static inline unsigned long parse_ptabs_read(unsigned long address,
                                             unsigned long *offset,
                                             unsigned long *flags)
{
	spinlock_t *ptl;
	pte_t *ptep;
	unsigned retry_count = 0;
	unsigned page_shift;
	unsigned long page_mask;

	local_irq_save(*flags);
retry:

	ptep = lookup_pte_lock(current->mm, address, &page_shift, &ptl);

	BUG_ON(!irqs_disabled());

#ifdef DEBUG_PARSE_PTABS_READ
	printk("ppr: pdir: %p, address: %lx, ptep: %p pte: %lx *ptep present: %u\n",
	       (pgd_t *)current->active_mm->pgd, address, ptep, pte_val(*ptep), pte_present(*ptep));
#endif

	while (ptep == NULL || !l4x_pte_present_user(*ptep)) {
		if (in_atomic())
			goto return_efault;

		if (l4x_do_page_fault(address, task_pt_regs(current),
		                      PF_EKERNEL | PF_EREAD | PF_ENOTPRESENT) == -1)
			goto return_efault;
		local_irq_disable();

		ptep = lookup_pte_lock(current->mm, address,
		                       &page_shift, &ptl);
	}

	if (!spin_trylock(ptl)) {
		if (in_atomic())
			goto return_efault;

		local_irq_enable();
		if (retry_count < 10) {
			retry_count++;
			cpu_relax();
		} else
			schedule_timeout_interruptible(1);
		local_irq_disable();
		goto retry;
	}
	*ptep   = pte_mkyoung(*ptep);
	spin_unlock(ptl);
	page_mask = ~0UL << page_shift;
	*offset = address & ~page_mask;
	return pte_val(*ptep) & page_mask & L4X_PHYSICAL_PAGE_MASK;

return_efault:
	local_irq_restore(*flags);
	return -EFAULT;
}

static inline unsigned long parse_ptabs_write(unsigned long address,
                                              unsigned long *offset,
                                              unsigned long *flags)
{
	spinlock_t *ptl;
	pte_t *ptep;
	unsigned retry_count = 0;
	unsigned page_shift;
	unsigned long page_mask;

	local_irq_save(*flags);

retry:
	ptep = lookup_pte_lock(current->mm, address, &page_shift, &ptl);

	// XXX: also check for user-bit as with read!?
	while (ptep == NULL || !l4x_pte_present_user(*ptep) || !pte_write(*ptep)) {
		if (in_atomic())
			goto return_efault;

#ifdef DEBUG_PARSE_PTABS_WRITE
		printk("ppw: pdir: %p, address: %lx, ptep: %p\n",
		       (pgd_t *)current->mm->pgd, address, ptep);
#endif
		if ((ptep == NULL) || !pte_present(*ptep)) {
			if (l4x_do_page_fault(address, task_pt_regs(current),
			                      PF_EKERNEL | PF_EWRITE | PF_ENOTPRESENT) == -1)
				goto return_efault;
		} else if (!pte_write(*ptep)) {
			if (l4x_do_page_fault(address, task_pt_regs(current),
			                      PF_EKERNEL | PF_EWRITE | PF_EPROTECTION) == -1)
				goto return_efault;
		}
		local_irq_disable();

		ptep = lookup_pte_lock(current->mm, address,
		                       &page_shift, &ptl);

#ifdef DEBUG_PARSE_PTABS_WRITE
		if (ptep)
			printk("pte_present(*ptep) = %lx pte_write(*ptep) = %x\n",
			       pte_present(*ptep), pte_write(*ptep));
#endif

	}

	if (!spin_trylock(ptl)) {
		if (in_atomic())
			goto return_efault;

		local_irq_enable();
		if (retry_count < 10) {
			retry_count++;
			cpu_relax();
		} else
			schedule_timeout_interruptible(1);
		local_irq_disable();
		goto retry;
	}
	*ptep   = pte_mkdirty(pte_mkyoung(*ptep));
	spin_unlock(ptl);
	page_mask = ~0UL << page_shift;
	*offset = address & ~page_mask;
	return pte_val(*ptep) & page_mask & L4X_PHYSICAL_PAGE_MASK;

return_efault:
	local_irq_restore(*flags);
	return -EFAULT;
}

#endif /* ! __ASM_L4__GENERIC__MEMORY_H__ */
