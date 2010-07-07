/*
 * Some processor specific functions.
 *
 * Maybe we should define an L4 CPU type somewhere?
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/highmem.h>

#include <asm/elf.h>
#include <asm/page.h>
#include <asm/procinfo.h>
#include <asm/tlbflush.h>

#include <l4/sys/kdebug.h>
#include <l4/sys/cache.h>

#include <asm/generic/memory.h>
#include <asm/generic/setup.h>

void cpu_sa1100_dcache_clean_area(void *addr, int sz)
{ l4_cache_clean_data((unsigned long)addr, (unsigned long)addr + sz - 1); }
void cpu_arm926_dcache_clean_area(void *addr, int sz)
{ l4_cache_clean_data((unsigned long)addr, (unsigned long)addr + sz - 1); }
void cpu_v6_dcache_clean_area(void *addr, int sz)
{ l4_cache_clean_data((unsigned long)addr, (unsigned long)addr + sz - 1); }

void cpu_sa1100_switch_mm(unsigned long pgd_phys, struct mm_struct *mm) {}
void cpu_arm926_switch_mm(unsigned long pgd_phys, struct mm_struct *mm) {}
void cpu_v6_switch_mm(unsigned long pgd_phys, struct mm_struct *mm) {}

extern unsigned long l4x_set_pte(struct mm_struct *mm, unsigned long addr, pte_t pteptr, pte_t pteval);
extern void          l4x_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t ptep);

static inline void l4x_cpu_set_pte_ext(pte_t *pteptr, pte_t pteval,
                                       unsigned int ext)
{
	if ((pte_val(*pteptr) & (L_PTE_PRESENT | L_PTE_MAPPED)) == (L_PTE_PRESENT | L_PTE_MAPPED)) {
		if (pteval == __pte(0))
			l4x_pte_clear(NULL, 0, *pteptr);
		else
			pte_val(pteval) = l4x_set_pte(NULL, 0, *pteptr, pteval);
	}
	*pteptr = pteval;
}

void cpu_sa1100_set_pte_ext(pte_t *pteptr, pte_t pteval, unsigned int ext)
{ l4x_cpu_set_pte_ext(pteptr, pteval, ext); }
void cpu_arm926_set_pte_ext(pte_t *pteptr, pte_t pteval, unsigned int ext)
{ l4x_cpu_set_pte_ext(pteptr, pteval, ext); }
void cpu_v6_set_pte_ext(pte_t *pteptr, pte_t pteval, unsigned int ext)
{ l4x_cpu_set_pte_ext(pteptr, pteval, ext); }


#ifdef CONFIG_L4_VCPU
extern void l4x_global_halt(void);
#endif
/*
 * cpu_do_idle()
 * Cause the processor to idle
 */
int cpu_sa1100_do_idle(void)
{
#ifdef CONFIG_L4_VCPU
	l4x_global_halt();
#endif
	return 0;
}
int cpu_arm926_do_idle(void)
{
#ifdef CONFIG_L4_VCPU
	l4x_global_halt();
#endif
	return 0;
}

int cpu_v6_do_idle(void)
{
#ifdef CONFIG_L4_VCPU
	l4x_global_halt();
#endif
	return 0;
}

void cpu_sa1100_proc_init(void) { printk("%s\n", __func__); }
void cpu_arm926_proc_init(void) { printk("%s\n", __func__); }
void cpu_v6_proc_init(void) { printk("%s\n", __func__); }

/*
 * cpu_proc_fin()
 *
 * Prepare the CPU for reset:
 *  - Disable interrupts
 *  - Clean and turn off caches.
 */
void cpu_sa1100_proc_fin(void) { local_irq_disable(); }
void cpu_arm926_proc_fin(void) { local_irq_disable(); }
#ifdef CONFIG_CPU_V6
void cpu_v6_proc_fin(void) { local_irq_disable(); }
#endif

void  __attribute__((noreturn)) l4x_cpu_reset(unsigned long addr)
{
	printk("%s called.\n", __func__);
	l4x_exit_l4linux();
	while (1)
		;
}

static inline void __copy_user_highpage(struct page *to, struct page *from,
                                        unsigned long vaddr, struct vm_area_struct *vma)
{
	void *kto, *kfrom;

        kfrom = kmap_atomic(from, KM_USER0);
        kto = kmap_atomic(to, KM_USER1);
        copy_page(kto, kfrom);
        kunmap_atomic(kto, KM_USER1);
        kunmap_atomic(kfrom, KM_USER0);
}

void v4wb_copy_user_highpage(struct page *to, struct page *from,
                             unsigned long vaddr, struct vm_area_struct *vma)
{
	__copy_user_highpage(to, from, vaddr, vma);
}
void arm926_copy_user_highpage(struct page *to, struct page *from,
                               unsigned long vaddr, struct vm_area_struct *vma)
{
	__copy_user_highpage(to, from, vaddr, vma);
}

static inline void __clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *kaddr = kmap_atomic(page, KM_USER0);
	clear_page(kaddr);
	kunmap_atomic(kaddr, KM_USER0);
}

void v4wb_clear_user_highpage(struct page *page, unsigned long vaddr)
{
	__clear_user_highpage(page, vaddr);
}
void arm926_clear_user_highpage(struct page *page, unsigned long vaddr)
{
	__clear_user_highpage(page, vaddr);
}

void v4wbi_flush_user_tlb_range(unsigned long start, unsigned long end,
                               struct vm_area_struct *mm)
{}

#ifdef CONFIG_CPU_V6
void v6wbi_flush_user_tlb_range(unsigned long start, unsigned long end,
                               struct vm_area_struct *mm)
{
}
#endif

void arm926_flush_user_cache_range(unsigned long start, unsigned long end,
                                 unsigned int flags)
{
	pgd_t *pgd;

	if (current->mm)
		pgd = (pgd_t *)current->mm->pgd;
	else if (current->active_mm)
		pgd = (pgd_t *)current->active_mm->pgd;
	else {
		printk("active_mm: No mm... %lx-%lx\n", start, end);
		return;
	}

	for (start &= PAGE_MASK; start < end; start += PAGE_SIZE) {
		pte_t *ptep = lookup_pte(pgd, start);
		if (ptep && pte_present(*ptep) && pte_mapped(*ptep)) {
			unsigned long k = pte_pfn(*ptep) << PAGE_SHIFT;
			unsigned long e = k + PAGE_SIZE;
			l4_cache_flush_data(k, e);
		}
	}
}

void arm926_flush_user_cache_all(void)
{
}

void v4wbi_flush_kern_tlb_range(unsigned long start, unsigned long end)
{
}

#ifdef CONFIG_CPU_V6
void v6wbi_flush_kern_tlb_range(unsigned long start, unsigned long end)
{
}
#endif

void arm926_flush_kern_cache_all(void)
{
	printk("arm926_flush_kern_cache_all()\n");
}

void arm926_coherent_kern_range(unsigned long start, unsigned long end)
{
	l4_cache_coherent(start, end - 1);
}

void arm926_coherent_user_range(unsigned long start, unsigned long end)
{
	pgd_t *pgd;

	if (current->mm)
		pgd = (pgd_t *)current->mm->pgd;
	else if (current->active_mm)
		pgd = (pgd_t *)current->active_mm->pgd;
	else {
		printk("active_mm: No mm... %lx-%lx\n", start, end);
		return;
	}

	for (start &= PAGE_MASK; start < end; start += PAGE_SIZE) {
		pte_t *ptep = lookup_pte(pgd, start);
		if (ptep && pte_present(*ptep) && pte_mapped(*ptep)) {
			unsigned long k = pte_pfn(*ptep) << PAGE_SHIFT;
			unsigned long e = k + PAGE_SIZE;
			l4_cache_clean_data(k, e);
		}
	}
}

void arm926_flush_kern_dcache_area(void *x, size_t size)
{
	l4_cache_clean_data((unsigned long)x,
	                    (unsigned long)x + size - 1);
}

#if 0
void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	printk("%s %d\n", __func__, __LINE__);
	//outstring("update_mmu_cache\n");
}
#endif

void l4x_flush_icache_all(void)
{
}

static void __data_abort(unsigned long pc)
{
	printk("%s called.\n", __func__);
}

void arm926_dma_flush_range(const void *start, const void *stop)
{
	printk("%s(%p, %p) called.\n", __func__, start, stop);
	l4_cache_flush_data((unsigned long)start, (unsigned long)stop);
}

void arm926_dma_unmap_area(const void *start, size_t sz, int direction)
{
	printk("%s(%p, %zd, %d) called.\n", __func__, start, sz, direction);
}

void arm926_dma_map_area(const void *start, size_t sz, int direction)
{
	printk("%s(%p, %zd, %d) called.\n", __func__, start, sz, direction);
}

/*
 * Could be that we need at least some of the definitions in MULTI only?.
 * That's why we just include this multi32.h file down here.
 */

#undef cpu_proc_init
#undef cpu_proc_fin
#undef cpu_reset
#undef cpu_do_idle
#undef cpu_dcache_clean_area
#undef cpu_set_pte_ext
#undef cpu_do_switch_mm
#include <asm/cpu-multi32.h>
#include <asm/cacheflush.h>

static struct processor l4_proc_fns = {
	._data_abort         = __data_abort,
	._proc_init          = cpu_sa1100_proc_init,
	._proc_fin           = cpu_sa1100_proc_fin,
	.reset               = l4x_cpu_reset,
	._do_idle            = cpu_sa1100_do_idle,
	.dcache_clean_area   = cpu_sa1100_dcache_clean_area,
	.switch_mm           = cpu_sa1100_switch_mm,
	.set_pte_ext         = cpu_sa1100_set_pte_ext,
};

static struct cpu_tlb_fns l4_tlb_fns = {
	.flush_user_range    = v4wbi_flush_user_tlb_range,
	.flush_kern_range    = v4wbi_flush_kern_tlb_range,
	.tlb_flags           = 0,
};

static struct cpu_user_fns l4_cpu_user_fns = {
	.cpu_clear_user_highpage = v4wb_clear_user_highpage,
	.cpu_copy_user_highpage  = v4wb_copy_user_highpage,
};

static struct cpu_cache_fns l4_cpu_cache_fns = {
	.flush_kern_all         = arm926_flush_kern_cache_all,
	.flush_user_all         = arm926_flush_user_cache_all,
	.flush_user_range       = arm926_flush_user_cache_range,
	.coherent_kern_range    = arm926_coherent_kern_range,
	.coherent_user_range    = arm926_coherent_user_range,
	.flush_kern_dcache_area = arm926_flush_kern_dcache_area,
	.dma_map_area           = arm926_dma_map_area,
	.dma_unmap_area         = arm926_dma_unmap_area,
	.dma_flush_range	= arm926_dma_flush_range,
};

static struct proc_info_list l4_proc_info __attribute__((__section__(".proc.info.init"))) = {
	.cpu_val         = 0,
	.cpu_mask        = 0,
	.__cpu_mm_mmu_flags = 0,
	.__cpu_io_mmu_flags = 0,
	.__cpu_flush     = 0,
#ifndef CONFIG_CPU_V6
	.arch_name       = "armv5",
	.elf_name        = "v5",
#else
	.arch_name       = "armv6",
	.elf_name        = "v6",
#endif
	.elf_hwcap       = HWCAP_SWP | HWCAP_THUMB | HWCAP_HALF | HWCAP_26BIT | HWCAP_FAST_MULT,
	.cpu_name        = "Fiasco",
	.proc            = &l4_proc_fns,
	.tlb             = &l4_tlb_fns,
	.user            = &l4_cpu_user_fns,
	.cache           = &l4_cpu_cache_fns,
};

/*
 * This is the only processor info for now, so keep lookup_processor_type
 * simple.
 */
struct proc_info_list *lookup_processor_type(void);
struct proc_info_list *lookup_processor_type(void)
{
	return &l4_proc_info;
}
