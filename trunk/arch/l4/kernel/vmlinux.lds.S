/*
 * The location of this file is hard coded in the top-level Makefiles
 */
#ifdef ARCH_x86
#include "arch-x86/vmlinux.lds.S"
#endif
#ifdef ARCH_arm
#include "arch-arm/vmlinux.lds.S"
#endif
