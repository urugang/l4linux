#
# Build rules for the FPU support code:
#

obj-y				+= init.o bugs.o core.o regset.o signal.o xstate.o

include arch/l4/Makefile.lib

$(call L4X_FILEREF, ../../../../x86/kernel/fpu, core)
