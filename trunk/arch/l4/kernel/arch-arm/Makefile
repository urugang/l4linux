#
# arch/l4/kernel/arch-arm/Makefile
#

obj-y		:= process.o sys_arm.o time.o traps.o stacktrace.o \
                   switch_to.o setup.o l4.o signal.o irq.o \
                   ptrace.o entry.o dispatch.o mach_setup.o io.o elf.o \
                   return_address.o head.o opcodes.o sigreturn_codes.o \
                   reboot.o

obj-$(CONFIG_ATAGS)		+= atags_parse.o
obj-$(CONFIG_ATAGS_PROC)	+= atags_proc.o
obj-$(CONFIG_DEPRECATED_PARAM_STRUCT) += atags_compat.o

obj-$(CONFIG_CPU_IDLE)		+= cpuidle.o
obj-$(CONFIG_ISA_DMA_API)	+= dma.o
obj-$(CONFIG_MODULES)		+= armksyms.o module.o
obj-$(CONFIG_ARM_MODULE_PLTS)	+= module-plts.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SMP)		+= platsmp.o
ifdef CONFIG_MMU
obj-$(CONFIG_SMP)		+= smp_tlb.o
endif
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_ARM_UNWIND)	+= unwind.o
obj-$(CONFIG_OABI_COMPAT)       += sys_oabi-compat.o
obj-$(CONFIG_CRASH_DUMP)	+= crash_dump.o
obj-$(CONFIG_OF)		+= devtree.o
obj-$(CONFIG_SWP_EMULATE)	+= swp_emulate.o
obj-$(CONFIG_ARM_ARCH_TIMER)	+= arch_timer.o
obj-$(CONFIG_KGDB)		+= kgdb.o patch.o
CFLAGS_swp_emulate.o		:= -Wa,-march=armv7-a

obj-$(CONFIG_DEBUG_LL)		+= debug.o
obj-$(CONFIG_ARM_CPU_TOPOLOGY)  += topology.o
obj-$(CONFIG_VDSO)		+= vdso.o

obj-$(CONFIG_ARM_VIRT_EXT)                      += hyp-stub.o

obj-$(CONFIG_L4_VPCI)				+= bios32.o

include arch/l4/Makefile.lib

$(call L4X_FILEREF, ../../../arm/kernel,                  \
       atags_parse atags_proc compat cpuidle crash_dump   \
       dma elf hyp-stub io irq kgdb module opcodes patch  \
       return_address sigreturn_codes stacktrace          \
       swp_emulate sys_oabi-compat time topology unwind)

KBUILD_CPPFLAGS	+= -I$(srctree)/arch/arm/kernel

CPPFLAGS_vmlinux.lds += -DTEXT_OFFSET=0x0100000

CFLAGS_REMOVE_return_address.o = -pg


# ----------------------------------------------------------------------
# _upage_start must(!) be last so that ld gets its calculation right!
define get_upage_defsyms
	upage_base=0xbf008000;						\
	for i in $$($(NM) $< | cut -d\  -f3 | grep -vw l4x_upage_start | grep -ie '^[a-z]');	\
	do								\
		echo -defsym $$i=$$i-l4x_upage_start+$$upage_base;	\
	done;								\
	echo -defsym l4x_upage_start=$$upage_base;
endef

obj-y		+= upage.reloced.o
targets		+= upage.o

quiet_cmd_upage = UPAGE   $@
      cmd_upage = $(LD) $(EXTRA_LDFLAGS) -N \
                        $(shell $(get_upage_defsyms)) -T /dev/null -o $@ $<

$(obj)/upage.reloced.o: $(obj)/upage.o FORCE
	$(call if_changed,upage)
