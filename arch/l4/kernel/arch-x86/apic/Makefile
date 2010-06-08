
obj-$(CONFIG_X86_LOCAL_APIC)    += apic.o apic_noop.o probe_$(BITS).o ipi.o nmi.o
obj-$(CONFIG_X86_IO_APIC)       += io_apic.o
obj-$(CONFIG_SMP)               += ipi.o

# -------------------------------------------
b		:= ../../../../x86/kernel/apic
apic_noop-y	:= $(b)/apic_noop.o
nmi-y		:= $(b)/nmi.o
probe_32-y	:= $(b)/probe_32.o
