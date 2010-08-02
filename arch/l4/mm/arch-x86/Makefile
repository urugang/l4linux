#
# arch/l4/mm/Makefile
#


obj-y	:= fault.o extable.o init.o init_32.o ioremap.o pageattr.o pgtable_32.o pgtable.o mmap.o \
           pat.o setup_nx.o

obj-$(CONFIG_X86_PAT)		+= pat_rbtree.o
obj-$(CONFIG_SMP)		+= tlb.o
obj-$(CONFIG_HUGETLB_PAGE)	+= hugetlbpage.o

extable-y	+= ../../../x86/mm/extable.o
mmap-y		+= ../../../x86/mm/mmap.o
pat-y		+= ../../../x86/mm/pat.o
pageattr-y	+= ../../../x86/mm/pageattr.o
pgtable_32-y	+= ../../../x86/mm/pgtable_32.o
setup_nx-y	+= ../../../x86/mm/setup_nx.o
hugetlbpage-y	+= ../../../x86/mm/hugetlbpage.o

EXTRA_CFLAGS    += -Iarch/x86/mm
