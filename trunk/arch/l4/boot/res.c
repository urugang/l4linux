
#include <string.h>
#include <l4/sys/compiler.h>


#ifdef ARCH_arm
asm(
	".global __l4_external_resolver\n"
	"__l4_external_resolver: \n"
	"	stmdb  sp!, {r0 - r12, lr} \n" // 56 bytes onto the stack
	"	ldr r0, [sp, #60] \n" // r0 is the jmptblentry
	"	ldr r1, [sp, #56] \n" // r1 is the funcname pointer
	"	bl __C__l4_external_resolver \n"
	"	str r0, [sp, #60] \n"
	"	ldmia sp!, {r0 - r12, lr}\n"
	"	add sp, sp, #4 \n"
	"	ldmia sp!, {pc} \n"
   );
#else
asm(
	".global __l4_external_resolver\n"
	"__l4_external_resolver: \n"
	"	pusha\n"
	"	mov 0x24(%esp), %eax\n" // eax is the jmptblentry
	"	mov 0x20(%esp), %edx\n" // edx is the symtab_ptr
	"	mov     (%edx), %edx\n" // edx is the funcname pointer
	"	call __C__l4_external_resolver \n"
	"	mov %eax, 0x20(%esp) \n"
	"	popa\n"
	"	ret $4\n"
   );
#endif


#define EF(func) \
	void func(void);
#include <func_list.h>

#include <stdio.h>
#undef EF
#define EF(func) \
	else if (!strcmp(L4_stringify(func), funcname)) \
             { p = func; }

void do_resolve_error(const char *funcname);

unsigned long
#ifdef ARCH_x86
__attribute__((regparm(3)))
#endif
__C__l4_external_resolver(unsigned long jmptblentry, char *funcname);

#include <l4/sys/kdebug.h>
unsigned long
#ifdef ARCH_x86
__attribute__((regparm(3)))
#endif
__C__l4_external_resolver(unsigned long jmptblentry, char *funcname)
{
	void *p;

	if (0) {
	}
#include <func_list.h>
	else
		p = 0;

	if (!p)
		do_resolve_error(funcname);

	*(unsigned long *)jmptblentry = (unsigned long)p;
	return (unsigned long)p;
}
