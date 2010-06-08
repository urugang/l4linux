#
# arch/l4/kernel/arch-x86/Makefile
#

obj-y		:= dispatch.o entry.o ioport.o \
		   ldt.o pci-dma.o pci-nommu.o process.o process_32.o ptrace.o reboot.o \
		   setup.o signal.o sys_i386.o time.o traps.o e820.o \
		   unimpl.o topology.o alternative.o tsc.o \
		   tls.o step.o rtc.o io_delay.o memory.o \
		   probe_roms_32.o kdebugfs.o x86_init.o i8259.o

obj-y				+= i387.o xsave.o
obj-y				+= dumpstack_32.o
obj-y				+= irq.o irq_32.o irq_l4.o irqinit.o
obj-y				+= dumpstack.o dumpstack_32.o
obj-y				+= hw_breakpoint.o

obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-y				+= cpu/
obj-$(CONFIG_ACPI)		+= acpi.o
obj-$(CONFIG_MODULES)		+= module.o
obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_K8_NB)		+= k8.o
obj-$(CONFIG_PCI)               += early-quirks.o
obj-$(CONFIG_X86_DS)		+= ds.o
obj-$(CONFIG_SMP)		+= apic/
obj-$(CONFIG_SMP)		+= mpparse.o smp.o \
                                   smpboot.o trampoline.o setup_percpu.o

# Make sure this is linked after any other paravirt_ops structs: see head.S
obj-$(CONFIG_PARAVIRT)		+= paravirt.o

# -------------------------------------------
b		:= ../../../x86/kernel
alternative-y	:= $(b)/alternative.o
ldt-y		:= $(b)/ldt.o
module-y	:= $(b)/module.o
pci-nommu-y	:= $(b)/pci-nommu.o
pci-dma-y	:= $(b)/pci-dma.o
sys_i386-y	:= $(b)/sys_i386_32.o
topology-y	:= $(b)/topology.o
i387-y		:= $(b)/i387.o
xsave-y		:= $(b)/xsave.o
k8-y		+= $(b)/k8.o
stacktrace-y	+= $(b)/stacktrace.o
tls-y		+= $(b)/tls.o
step-y		+= $(b)/step.o
ds-y		+= $(b)/ds.o
io_delay-y	+= $(b)/io_delay.o
trampoline-y	+= $(b)/trampoline.o
setup_percpu-y  += $(b)/setup_percpu.o
probe_roms_32-y	+= $(b)/probe_roms_32.o
early-quirks-y	+= $(b)/early-quirks.o
kdebugfs-y	+= $(b)/kdebugfs.o
dumpstack_32-y	+= $(b)/dumpstack_32.o
dumpstack-y	+= $(b)/dumpstack.o
hw_breakpoint-y += $(b)/hw_breakpoint.o
i8259-y         += $(b)/i8259.o

# we have our own versions of destroy_context and init_new_context,
# so rename the original one
CFLAGS_ldt.o    += -Ddestroy_context=destroy_context_origarch
CFLAGS_ldt.o    += -Dinit_new_context=init_new_context_origarch

# syscall_table_32.S
KBUILD_AFLAGS	+= -Iarch/x86/kernel
# headers
KBUILD_CFLAGS	+= -Iarch/x86/kernel
