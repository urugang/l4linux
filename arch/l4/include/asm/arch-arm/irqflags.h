#ifndef __ASM__L4__ARCH_ARM__IRQFLAGS_H__
#define __ASM__L4__ARCH_ARM__IRQFLAGS_H__

#ifdef __KERNEL__

#include <asm/ptrace.h>

/*
 * CPU interrupt mask handling.
 */
#ifndef  CONFIG_L4
#if __LINUX_ARM_ARCH__ >= 6

#define raw_local_irq_save(x)					\
	({							\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_irq_save\n"	\
	"cpsid	i"						\
	: "=r" (x) : : "memory", "cc");				\
	})

#define raw_local_irq_enable()  __asm__("cpsie i	@ __sti" : : : "memory", "cc")
#define raw_local_irq_disable() __asm__("cpsid i	@ __cli" : : : "memory", "cc")
#define local_fiq_enable()  __asm__("cpsie f	@ __stf" : : : "memory", "cc")
#define local_fiq_disable() __asm__("cpsid f	@ __clf" : : : "memory", "cc")

#else

/*
 * Save the current interrupt enable state & disable IRQs
 */
#define raw_local_irq_save(x)					\
	({							\
		unsigned long temp;				\
		(void) (&temp == &x);				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_irq_save\n"	\
"	orr	%1, %0, #128\n"					\
"	msr	cpsr_c, %1"					\
	: "=r" (x), "=r" (temp)					\
	:							\
	: "memory", "cc");					\
	})
	
/*
 * Enable IRQs
 */
#define raw_local_irq_enable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_irq_enable\n"	\
"	bic	%0, %0, #128\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

/*
 * Disable IRQs
 */
#define raw_local_irq_disable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_irq_disable\n"	\
"	orr	%0, %0, #128\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

/*
 * Enable FIQs
 */
#define local_fiq_enable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ stf\n"		\
"	bic	%0, %0, #64\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

/*
 * Disable FIQs
 */
#define local_fiq_disable()					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ clf\n"		\
"	orr	%0, %0, #64\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory", "cc");					\
	})

#endif
#else /* L4 */

#include <asm/generic/irq.h>
#include <asm/generic/vcpu.h>

#ifndef CONFIG_L4_TAMED
#include <l4/sys/kdebug.h>

  #define raw_local_save_flags(x)	do { (x) = l4x_local_save_flags(); } while (0)
  #define raw_local_irq_restore(x)	do { l4x_local_irq_restore(x); } while (0)
  #define l4x_real_irq_disable()	do { l4_sys_cli(); } while (0)
  #define l4x_real_irq_enable()		do { l4_sys_sti(); } while (0)
  #define raw_local_irq_disable()	do { l4x_local_irq_disable(); } while (0)
  #define raw_local_irq_enable()	do { l4x_local_irq_enable(); } while (0)

#else

  extern void l4x_global_cli(void);
  extern void l4x_global_sti(void);
  extern unsigned long l4x_global_save_flags(void);
  extern void l4x_global_restore_flags(unsigned long flags);

  #define raw_local_save_flags(x)	do { (x) = l4x_global_save_flags(); } while (0)
  #define raw_local_irq_restore(x)	do { l4x_global_restore_flags(x); } while (0)
  #define l4x_real_irq_disable()	BUG()
  #define l4x_real_irq_enable()		BUG()
  #define raw_local_irq_disable()	do { l4x_global_cli(); } while (0)
  #define raw_local_irq_enable()	do { l4x_global_sti(); } while (0)

#endif

#define local_fiq_enable()	do { } while (0)

#define raw_local_irq_save(x)	do { local_save_flags(x); local_irq_disable(); } while (0)

#endif /* L4 */



#ifndef CONFIG_L4
/*
 * Save the current interrupt enable state.
 */
#define raw_local_save_flags(x)					\
	({							\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ local_save_flags"	\
	: "=r" (x) : : "memory", "cc");				\
	})

/*
 * restore saved IRQ & FIQ state
 */
#define raw_local_irq_restore(x)				\
	__asm__ __volatile__(					\
	"msr	cpsr_c, %0		@ local_irq_restore\n"	\
	:							\
	: "r" (x)						\
	: "memory", "cc")

#define raw_irqs_disabled_flags(flags)	\
({					\
	(int)((flags) & PSR_I_BIT);	\
})

#else /* L4 */

#ifdef CONFIG_L4_VCPU
#define raw_irqs_disabled_flags(flags)		\
({						\
	(int)(!((flags) & L4_VCPU_F_IRQ));	\
})
#else
#define raw_irqs_disabled_flags(flags)		\
({						\
	(int)((flags) == L4_IRQ_DISABLED);	\
})
#endif /* vCPU */
#endif /* L4 */

#endif /* __KERNEL__ */

#endif /* __ASM__L4__ARCH_ARM__IRQFLAGS_H__ */
