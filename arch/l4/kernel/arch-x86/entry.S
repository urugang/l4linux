/*
 *  linux/arch/l4/kernel/arch-i386/entry.S
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/dwarf2.h>
#include <asm/ftrace.h>
#include <asm/irq_vectors.h>

// just dummy values so that they are defined
#define SPURIOUS_APIC_VECTOR 20
#define ERROR_APIC_VECTOR    21


.section .rodata,"a"
#include "syscall_table_32.S"


syscall_table_size=(.-sys_call_table)
.globl nr_syscalls
nr_syscalls=(.-sys_call_table)/4

.macro RING0_INT_FRAME
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA esp, 3*4
	/*CFI_OFFSET cs, -2*4;*/
	CFI_OFFSET eip, -3*4
.endm

.macro RING0_EC_FRAME
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA esp, 4*4
	/*CFI_OFFSET cs, -2*4;*/
	CFI_OFFSET eip, -3*4
.endm


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
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
.endm
.macro POP_GS pop=0
	addl $(4 + \pop), %esp
	CFI_ADJUST_CFA_OFFSET -(4 + \pop)
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
	pushl %gs
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET gs, 0*/
.endm

.macro POP_GS pop=0
98:	popl %gs
	CFI_ADJUST_CFA_OFFSET -4
	/*CFI_RESTORE gs*/
  .if \pop <> 0
	add $\pop, %esp
	CFI_ADJUST_CFA_OFFSET -\pop
  .endif
.endm
.macro POP_GS_EX
.pushsection .fixup, "ax"
99:	movl $0, (%esp)
	jmp 98b
.section __ex_table, "a"
	.align 4
	.long 98b, 99b
.popsection
.endm

.macro PTGS_TO_GS
98:	mov PT_GS(%esp), %gs
.endm
.macro PTGS_TO_GS_EX
.pushsection .fixup, "ax"
99:	movl $0, PT_GS(%esp)
	jmp 98b
.section __ex_table, "a"
	.align 4
	.long 98b, 99b
.popsection
.endm

.macro GS_TO_REG reg
	movl %gs, \reg
	/*CFI_REGISTER gs, \reg*/
.endm
.macro REG_TO_PTGS reg
	movl \reg, PT_GS(%esp)
	/*CFI_REL_OFFSET gs, PT_GS*/
.endm
.macro SET_KERNEL_GS reg
	movl $(__KERNEL_STACK_CANARY), \reg
	movl \reg, %gs
.endm

#endif	/* CONFIG_X86_32_LAZY_GS */
.macro SAVE_ALL
	cld
	PUSH_GS
	pushl %fs
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET fs, 0;*/
	pushl %es
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET es, 0;*/
	pushl %ds
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET ds, 0;*/
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET eax, 0
	pushl %ebp
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ebp, 0
	pushl %edi
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edi, 0
	pushl %esi
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET esi, 0
	pushl %edx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edx, 0
	pushl %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx, 0
	pushl %ebx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ebx, 0
	movl $(__USER_DS), %edx
	movl %edx, %ds
	movl %edx, %es
	movl $(__KERNEL_PERCPU), %edx
	movl %edx, %fs
	SET_KERNEL_GS %edx
.endm

/*
 *  Irq entries should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"
#define BUILD_INTERRUPT3(name, nr, fn)	\
ENTRY(name)				\
	RING0_INT_FRAME;		\
	pushl $~(nr);			\
	CFI_ADJUST_CFA_OFFSET 4;	\
	SAVE_ALL;			\
	TRACE_IRQS_OFF			\
	movl %esp,%eax;			\
	call fn;			\
	jmp ret_from_intr;		\
	CFI_ENDPROC;			\
ENDPROC(name)

#define BUILD_INTERRUPT(name, nr)	BUILD_INTERRUPT3(name, nr, smp_##name)


/* The include is where all of the SMP etc. interrupts come from */
//#include <asm/entry_arch.h>

ENTRY(coprocessor_error)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_coprocessor_error
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(coprocessor_error)

ENTRY(simd_coprocessor_error)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_simd_coprocessor_error
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(simd_coprocessor_error)

ENTRY(device_not_available)
	RING0_INT_FRAME
	pushl $-1			# mark this as an int
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_device_not_available
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(device_not_available)

#ifdef CONFIG_PARAVIRT
ENTRY(native_iret)
	iret
.section __ex_table,"a"
	.align 4
	.long native_iret, iret_exc
.previous
END(native_iret)

ENTRY(native_irq_enable_sysexit)
	sti
	sysexit
END(native_irq_enable_sysexit)
#endif

ENTRY(overflow)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_overflow
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(overflow)

ENTRY(bounds)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_bounds
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(bounds)

ENTRY(invalid_op)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_invalid_op
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(invalid_op)

ENTRY(coprocessor_segment_overrun)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_coprocessor_segment_overrun
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(coprocessor_segment_overrun)

ENTRY(invalid_TSS)
	RING0_EC_FRAME
	pushl $do_invalid_TSS
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(invalid_TSS)

ENTRY(segment_not_present)
	RING0_EC_FRAME
	pushl $do_segment_not_present
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(segment_not_present)

ENTRY(stack_segment)
	RING0_EC_FRAME
	pushl $do_stack_segment
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(stack_segment)

ENTRY(alignment_check)
	RING0_EC_FRAME
	pushl $do_alignment_check
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(alignment_check)

ENTRY(divide_error)
	RING0_INT_FRAME
	pushl $0			# no error code
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_divide_error
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(divide_error)

#ifdef CONFIG_X86_MCE
ENTRY(machine_check)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl machine_check_vector
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(machine_check)
#endif

ENTRY(spurious_interrupt_bug)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_spurious_interrupt_bug
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
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

#ifdef CONFIG_L4_VCPU
ENTRY(kernel_thread_helper)
//	pushl $0                # fake return address for unwinder
	CFI_STARTPROC
	movl %edi,%eax
	call *%esi
	call do_exit
	ud2                     # padding for call trace
	CFI_ENDPROC
ENDPROC(kernel_thread_helper)
#endif

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
