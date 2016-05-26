# Kernel does not boot with instrumentation of tlb.c.
KCOV_INSTRUMENT_tlb.o	:= n

obj-y	:=  init.o init_$(BITS).o fault.o ioremap.o extable.o pageattr.o mmap.o \
	    pat.o pgtable.o physaddr.o setup_nx.o

# Make sure __phys_addr has no stackprotector
nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_physaddr.o		:= $(nostackp)
CFLAGS_setup_nx.o		:= $(nostackp)

CFLAGS_fault.o := -I$(src)/../../../x86/include/asm/trace

obj-$(CONFIG_X86_PAT)		+= pat_rbtree.o
obj-$(CONFIG_SMP)		+= tlb.o

obj-$(CONFIG_X86_32)		+= pgtable_32.o

obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o
obj-$(CONFIG_X86_PTDUMP_CORE)	+= dump_pagetables.o
obj-$(CONFIG_X86_PTDUMP)	+= debug_pagetables.o

obj-$(CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS) += pkeys.o

include arch/l4/Makefile.lib

$(call L4X_FILEREF, ../../../x86/mm,               \
       extable mmap pat pageattr pgtable_32        \
       setup_nx hugetlbpage debug_pagetables pkeys)

EXTRA_CFLAGS    += -Iarch/x86/mm
