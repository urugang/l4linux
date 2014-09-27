#
# arch/l4/mm/arch-arm/Makefile
#

obj-y		:= init.o flush.o ioremap.o proc-l4.o dma-mapping.o \
                   fault.o fault-armv.o extable.o mmap.o \
		   pgd.o mmu.o iomap.o idmap.o

obj-$(CONFIG_CPU_COPY_V6) += context.o
obj-$(CONFIG_ARM_PTDUMP)	+= dump.o
obj-$(CONFIG_MODULES)           += proc-syms.o

obj-$(CONFIG_OUTER_CACHE)	+= l2c-common.o

KBUILD_CPPFLAGS += -I$(srctree)/arch/arm/mm

extable-y	:= ../../../arm/mm/extable.o
fault-armv-y	:= ../../../arm/mm/fault-armv.o
mmap-y		:= ../../../arm/mm/mmap.o
iomap-y		:= ../../../arm/mm/iomap.o
idmap-y		:= ../../../arm/mm/idmap.o
dump-y		:= ../../../arm/mm/dump.o
proc-syms-y	:= ../../../arm/mm/proc-syms.o
l2c-common-y    := ../../../arm/mm/l2c-common.o
