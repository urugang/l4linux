/*
 *  Copyright (C) 1991,1992  Linus Torvalds
 *
 * entry_32.S contains the system-call and low-level fault and trap handling routines.
 *
 * Stack layout in 'syscall_exit':
 *	ptrace needs to have all registers on the stack.
 *	If the order here is changed, it needs to be
 *	updated in fork.c:copy_process(), signal.c:do_signal(),
 *	ptrace.c and ptrace.h
 *
 *	 0(%esp) - %ebx
 *	 4(%esp) - %ecx
 *	 8(%esp) - %edx
 *	 C(%esp) - %esi
 *	10(%esp) - %edi
 *	14(%esp) - %ebp
 *	18(%esp) - %eax
 *	1C(%esp) - %ds
 *	20(%esp) - %es
 *	24(%esp) - %fs
 *	28(%esp) - %gs		saved iff !CONFIG_X86_32_LAZY_GS
 *	2C(%esp) - orig_eax
 *	30(%esp) - %eip
 *	34(%esp) - %cs
 *	38(%esp) - %eflags
 *	3C(%esp) - %oldesp
 *	40(%esp) - %oldss
 */

#include <linux/linkage.h>
#include <linux/err.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/smp.h>
#include <asm/page_types.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/ftrace.h>
#include <asm/irq_vectors.h>
#include <asm/cpufeature.h>
#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/smap.h>

// just dummy values so that they are defined
#define SPURIOUS_APIC_VECTOR 20
#define ERROR_APIC_VECTOR    21


	.section .entry.text, "ax"

/*
 * User gs save/restore
 *
 * %gs is used for userland TLS and kernel only uses it for stack
 * canary which is required to be at %gs:20 by gcc.  Read the comment
 * at the top of stackprotector.h for more info.
 *
 * Local labels 98 and 99 are used.
 */
#ifdef CONFIG_X86_32_LAZY_GS

 /* unfortunately push/pop can't be no-op */
.macro PUSH_GS
	pushl	$0
.endm
.macro POP_GS pop=0
	addl	$(4 + \pop), %esp
.endm
.macro POP_GS_EX
.endm

 /* all the rest are no-op */
.macro PTGS_TO_GS
.endm
.macro PTGS_TO_GS_EX
.endm
.macro GS_TO_REG reg
.endm
.macro REG_TO_PTGS reg
.endm
.macro SET_KERNEL_GS reg
.endm

#else	/* CONFIG_X86_32_LAZY_GS */

.macro PUSH_GS
	pushl	%gs
.endm

.macro POP_GS pop=0
98:	popl	%gs
  .if \pop <> 0
	add	$\pop, %esp
  .endif
.endm
.macro POP_GS_EX
.pushsection .fixup, "ax"
99:	movl	$0, (%esp)
	jmp	98b
.popsection
	_ASM_EXTABLE(98b, 99b)
.endm

.macro PTGS_TO_GS
98:	mov	PT_GS(%esp), %gs
.endm
.macro PTGS_TO_GS_EX
.pushsection .fixup, "ax"
99:	movl	$0, PT_GS(%esp)
	jmp	98b
.popsection
	_ASM_EXTABLE(98b, 99b)
.endm

.macro GS_TO_REG reg
	movl	%gs, \reg
.endm
.macro REG_TO_PTGS reg
	movl	\reg, PT_GS(%esp)
.endm
.macro SET_KERNEL_GS reg
	movl	$(__KERNEL_STACK_CANARY), \reg
	movl	\reg, %gs
.endm

#endif /* CONFIG_X86_32_LAZY_GS */

.macro SAVE_ALL
	cld
	PUSH_GS
	pushl	%fs
	pushl	%es
	pushl	%ds
	pushl	%eax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	movl	$(__USER_DS), %edx
	movl	%edx, %ds
	movl	%edx, %es
	movl	$(__KERNEL_PERCPU), %edx
	movl	%edx, %fs
	SET_KERNEL_GS %edx
.endm

/*
 *  Irq entries should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"
#define BUILD_INTERRUPT3(name, nr, fn)	\
ENTRY(name)				\
	ASM_CLAC;			\
	pushl	$~(nr);			\
	SAVE_ALL;			\
	TRACE_IRQS_OFF			\
	movl	%esp, %eax;		\
	call	fn;			\
	jmp	ret_from_intr;		\
ENDPROC(name)


#ifdef CONFIG_TRACING
# define TRACE_BUILD_INTERRUPT(name, nr)	BUILD_INTERRUPT3(trace_##name, nr, smp_trace_##name)
#else
# define TRACE_BUILD_INTERRUPT(name, nr)
#endif

#define BUILD_INTERRUPT(name, nr)		\
	BUILD_INTERRUPT3(name, nr, smp_##name);	\
	TRACE_BUILD_INTERRUPT(name, nr)

/* The include is where all of the SMP etc. interrupts come from */
//#include <asm/entry_arch.h>

ENTRY(coprocessor_error)
	ASM_CLAC
	pushl	$0
	pushl	$do_coprocessor_error
	jmp	error_code
END(coprocessor_error)

ENTRY(simd_coprocessor_error)
	ASM_CLAC
	pushl	$0
#ifdef CONFIG_X86_INVD_BUG
	/* AMD 486 bug: invd from userspace calls exception 19 instead of #GP */
	ALTERNATIVE "pushl	$do_general_protection",	\
		    "pushl	$do_simd_coprocessor_error",	\
		    X86_FEATURE_XMM
#else
	pushl	$do_simd_coprocessor_error
#endif
	jmp	error_code
END(simd_coprocessor_error)

ENTRY(device_not_available)
	ASM_CLAC
	pushl	$-1				# mark this as an int
	pushl	$do_device_not_available
	jmp	error_code
END(device_not_available)

#ifdef CONFIG_PARAVIRT
ENTRY(native_iret)
	iret
	_ASM_EXTABLE(native_iret, iret_exc)
END(native_iret)

ENTRY(native_irq_enable_sysexit)
	sti
	sysexit
END(native_irq_enable_sysexit)
#endif

ENTRY(overflow)
	ASM_CLAC
	pushl	$0
	pushl	$do_overflow
	jmp	error_code
END(overflow)

ENTRY(bounds)
	ASM_CLAC
	pushl	$0
	pushl	$do_bounds
	jmp	error_code
END(bounds)

ENTRY(invalid_op)
	ASM_CLAC
	pushl	$0
	pushl	$do_invalid_op
	jmp	error_code
END(invalid_op)

ENTRY(coprocessor_segment_overrun)
	ASM_CLAC
	pushl	$0
	pushl	$do_coprocessor_segment_overrun
	jmp	error_code
END(coprocessor_segment_overrun)

ENTRY(invalid_TSS)
	ASM_CLAC
	pushl	$do_invalid_TSS
	jmp	error_code
END(invalid_TSS)

ENTRY(segment_not_present)
	ASM_CLAC
	pushl	$do_segment_not_present
	jmp	error_code
END(segment_not_present)

ENTRY(stack_segment)
	ASM_CLAC
	pushl	$do_stack_segment
	jmp	error_code
END(stack_segment)

ENTRY(alignment_check)
	ASM_CLAC
	pushl	$do_alignment_check
	jmp	error_code
END(alignment_check)

ENTRY(divide_error)
	ASM_CLAC
	pushl	$0				# no error code
	pushl	$do_divide_error
	jmp	error_code
END(divide_error)

#ifdef CONFIG_X86_MCE
ENTRY(machine_check)
	ASM_CLAC
	pushl	$0
	pushl	machine_check_vector
	jmp	error_code
END(machine_check)
#endif

ENTRY(spurious_interrupt_bug)
	ASM_CLAC
	pushl	$0
	pushl	$do_spurious_interrupt_bug
	jmp	error_code
END(spurious_interrupt_bug)

/*
 * End of kprobes section
 */
	.popsection

ret_from_intr:
	ud2

ENTRY(debug)
	ud2
END(debug)

ENTRY(nmi)
	ud2
END(nmi)


ENTRY(int3)
	ud2
END(int3)

error_code:
	ud2

ENTRY(general_protection)
	ud2
END(general_protection)

ENTRY(page_fault)
	ud2
END(page_fault)

ENTRY(system_call)
	ud2
END(system_call)

ENTRY(interrupt)
	ud2
END(interrupt)

ENTRY(ret_from_fork)
	pushl	%eax
	call	schedule_tail
	GET_THREAD_INFO(%ebp)
	popl	%eax
	pushl	$0x0202				# Reset kernel eflags
	popfl
	jmp	syscall_exit
END(ret_from_fork)

ENTRY(ret_from_kernel_thread)
	pushl	%eax
	call	schedule_tail
	GET_THREAD_INFO(%ebp)
	popl	%eax
	pushl	$0x0202				# Reset kernel eflags
	popfl
	movl	PT_EBP(%esp), %eax
	call	*PT_EBX(%esp)
	movl	$0, PT_EAX(%esp)
	jmp	syscall_exit
ENDPROC(ret_from_kernel_thread)

#ifdef CONFIG_SMP
#include <asm/thread_info.h>
.data
ENTRY(stack_start)
	.long init_thread_union+THREAD_SIZE
	.long 0

.align 4
ENTRY(initial_code)
	.long 0

.previous
#endif
