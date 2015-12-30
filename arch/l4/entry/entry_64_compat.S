
#include <linux/linkage.h>

.global stub32_clone
stub32_clone:
	xchg	%r8, %rcx
	jmp sys_clone

.macro mkstub name, compat
	ALIGN
GLOBAL(stub32_\name)
	jmp \compat
.endm

mkstub fork, sys_fork
mkstub vfork, sys_vfork
mkstub execve, compat_sys_execve
mkstub execveat, compat_sys_execveat
mkstub rt_sigreturn, sys32_rt_sigreturn
mkstub sigreturn, sys32_sigreturn
