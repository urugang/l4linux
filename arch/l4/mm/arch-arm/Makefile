#
# arch/l4/mm/arch-arm/Makefile
#

obj-y		:= init.o flush.o ioremap.o proc-l4.o dma-mapping.o \
                   fault.o fault-armv.o extable.o mmap.o \
		   pgd.o mmu.o iomap.o idmap.o pageattr.o

obj-$(CONFIG_CPU_COPY_V6) += context.o
obj-$(CONFIG_ARM_PTDUMP)	+= dump.o
obj-$(CONFIG_ALIGNMENT_TRAP)	+= alignment.o
obj-$(CONFIG_MODULES)           += proc-syms.o

obj-$(CONFIG_OUTER_CACHE)	+= l2c-common.o

KBUILD_CPPFLAGS += -I$(srctree)/arch/arm/mm

include arch/l4/Makefile.lib

$(call L4X_FILEREF, ../../../arm/mm,       \
       dump extable fault-armv idmap iomap \
       l2c-common mmap pageattr proc-syms)
