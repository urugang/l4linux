
obj-$(CONFIG_X86_LOCAL_APIC)    += apic.o apic_noop.o probe_$(BITS).o ipi.o
obj-$(CONFIG_X86_IO_APIC)       += io_apic.o
obj-$(CONFIG_SMP)               += ipi.o

ifeq ($(CONFIG_X86_64),y)
obj-y				+= apic_flat_64.o
endif

# -------------------------------------------
b		:= ../../../../x86/kernel/apic
apic_noop-y	:= $(b)/apic_noop.o
probe_$(BITS)-y	:= $(b)/probe_$(BITS).o
apic_flat_64-y  := $(b)/apic_flat_64.o
