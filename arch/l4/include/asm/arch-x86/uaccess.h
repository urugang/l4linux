#ifndef _ASM_X86_UACCESS_H
#define _ASM_X86_UACCESS_H
/*
 * User space memory access functions
 */
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <linux/string.h>
#include <asm/asm.h>
#include <asm/page.h>
#include <asm/smap.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS	MAKE_MM_SEG(-1UL)
#define USER_DS 	MAKE_MM_SEG(TASK_SIZE_MAX)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define segment_eq(a, b)	((a).seg == (b).seg)

#define user_addr_max() (current_thread_info()->addr_limit.seg)
#define __addr_ok(addr) 	\
	((unsigned long __force)(addr) < user_addr_max())

#ifdef CONFIG_L4
#define l4x_copy_to_user       copy_to_user
#define l4x_copy_from_user     copy_from_user
#define l4x_clear_user         clear_user
#define l4x_strncpy_from_user  strncpy_from_user
#define l4x_strnlen_user       strnlen_user
#define l4x_strlen_user        strlen_user
#endif

/*
 * Test whether a block of memory is a valid user space address.
 * Returns 0 if the range is valid, nonzero otherwise.
 */
static inline bool __chk_range_not_ok(unsigned long addr, unsigned long size, unsigned long limit)
{
	/*
	 * If we have used "sizeof()" for the size,
	 * we know it won't overflow the limit (but
	 * it might overflow the 'addr', so it's
	 * important to subtract the size from the
	 * limit, not add it to the address).
	 */
	if (__builtin_constant_p(size))
		return addr > limit - size;

	/* Arbitrary sizes? Be careful about overflow */
	addr += size;
	if (addr < size)
		return true;
	return addr > limit;
}

#define __range_not_ok(addr, size, limit)				\
({									\
	__chk_user_ptr(addr);						\
	__chk_range_not_ok((unsigned long __force)(addr), size, limit); \
})

/**
 * access_ok: - Checks if a user space pointer is valid
 * @type: Type of access: %VERIFY_READ or %VERIFY_WRITE.  Note that
 *        %VERIFY_WRITE is a superset of %VERIFY_READ - if it is safe
 *        to write to a block, it is always safe to read from it.
 * @addr: User space pointer to start of block to check
 * @size: Size of block to check
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Checks if a pointer to a block of memory in user space is valid.
 *
 * Returns true (nonzero) if the memory block may be valid, false (zero)
 * if it is definitely invalid.
 *
 * Note that, depending on architecture, this function probably just
 * checks that the pointer is in the user space range - after calling
 * this function, memory access functions may still return -EFAULT.
 */
#define access_ok(type, addr, size) \
	likely(!__range_not_ok(addr, size, user_addr_max()))

/*
 * The exception table consists of pairs of addresses relative to the
 * exception table enty itself: the first is the address of an
 * instruction that is allowed to fault, and the second is the address
 * at which the program should continue.  No registers are modified,
 * so it is entirely up to the continuation code to figure out what to
 * do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry {
	int insn, fixup;
};
/* This is not the generic standard exception_table_entry format */
#define ARCH_HAS_SORT_EXTABLE
#define ARCH_HAS_SEARCH_EXTABLE

extern int fixup_exception(struct pt_regs *regs);
extern int early_fixup_exception(unsigned long *ip);

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the ugliness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 */

extern long __get_user_1(u8  *val, const void *address);
extern long __get_user_2(u16 *val, const void *address);
extern long __get_user_4(u32 *val, const void *address);
extern long __get_user_8(u64 *val, const void *address);
extern long __get_user_bad(void);

/*
 * This is a type: either unsigned long, if the argument fits into
 * that type, or otherwise unsigned long long.
 */
#define __inttype(x) \
__typeof__(__builtin_choose_expr(sizeof(x) > sizeof(0UL), 0ULL, 0UL))

/**
 * get_user: - Get a simple variable from user space.
 * @x:   Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
/*
 * Careful: we have to cast the result to the type of the pointer
 * for sign reasons.
 *
 * The use of _ASM_DX as the register specifier is a bit of a
 * simplification, as gcc only cares about it as the starting point
 * and not size: for a 64-bit value it will use %ecx:%edx on 32 bits
 * (%ecx being the next register in gcc's x86 register sequence), and
 * %rdx on 64 bits.
 *
 * Clang/LLVM cares about the size of the register, but still wants
 * the base register for something that ends up being a pair.
 */
#define get_user(x, ptr)						\
({									\
	int __ret_gu;							\
 	unsigned long __val_gu;						\
	__chk_user_ptr(ptr);						\
	switch(sizeof (*(ptr))) {					\
	case 1:  __ret_gu = __get_user_1((unsigned char      *)&__val_gu,ptr); break;		\
	case 2:  __ret_gu = __get_user_2((unsigned short     *)&__val_gu,ptr); break;		\
	case 4:  __ret_gu = __get_user_4((unsigned int       *)&__val_gu,ptr); break;		\
	case 8:  __ret_gu = __get_user_8((unsigned long long *)&__val_gu,ptr); break;		\
	default: __ret_gu = __get_user_bad(); break;		\
	}								\
	(x) = (__force __typeof__(*(ptr)))__val_gu;				\
	__ret_gu;							\
})

#define __get_user_size_ex(x, ptr, size)				\
do {									\
	__chk_user_ptr(ptr);						\
	switch(size) {							\
	case 1:  __get_user_1((unsigned char      *)&x,ptr); break;		\
	case 2:  __get_user_2((unsigned short     *)&x,ptr); break;		\
	case 4:  __get_user_4((unsigned int       *)&x,ptr); break;		\
	case 8:  __get_user_8((unsigned long long *)&x,ptr); break;		\
	default: __get_user_bad(); break;			\
	}								\
} while (0)



extern long __put_user_1(u8  val, const void *address);
extern long __put_user_2(u16 val, const void *address);
extern long __put_user_4(u32 val, const void *address);
extern long __put_user_8(u64 val, const void *address);
extern long __put_user_bad(void);



/**
 * put_user: - Write a simple value into user space.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#if 0
#define put_user(x,ptr)							\
  __put_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))
#endif

/*
 * To avoid warnings from the compiler (in case we want to cast a pointer to
 * u64) we use this dirty asm wrapper trick.
 */
#ifdef CONFIG_X86_32
#define __put_user_8_wrap(x, addr, retval)				\
	({ unsigned long dummy;                                         \
	__asm__ __volatile__ (						\
		"call __put_user_8	\n\t"				\
		: "=a" (retval), "=d" (dummy), "=c" (dummy)		\
		: "A" (x), "c" (addr));                                 \
	 })
#else
#define __put_user_8_wrap(x, addr, retval)				\
        retval = __put_user_8((u64)x,addr);
#endif

#define put_user_macro(x,ptr)						\
	({								\
	int __ret_pu;							\
	__typeof__(*(ptr)) __pu_val;					\
	__chk_user_ptr(ptr);						\
	__pu_val = (__typeof__(*(ptr)))(x);							\
	switch (sizeof(*(ptr))) {					\
	case 1:  __ret_pu = __put_user_1(( u8)((unsigned long)__pu_val),ptr); break; \
	case 2:  __ret_pu = __put_user_2((u16)((unsigned long)__pu_val),ptr); break; \
	case 4:  __ret_pu = __put_user_4((u32)((unsigned long)__pu_val),ptr); break; \
	case 8:  __put_user_8_wrap((__pu_val), ptr, __ret_pu); break; \
	default: __ret_pu = __put_user_bad(); break;			\
	}								\
	__ret_pu;							\
	})
	

#define __get_user get_user
#define __put_user(x,ptr) put_user_macro((__typeof__(*(ptr)))(x), (ptr))
#define put_user put_user_macro

#define __get_user_unaligned __get_user
#define __put_user_unaligned __put_user

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))


/*
 * uaccess_try and catch
 */
#define uaccess_try	do {						\
	current_thread_info()->uaccess_err = 0;				\
	stac();								\
	barrier();

#define uaccess_catch(err)						\
	clac();								\
	(err) |= (current_thread_info()->uaccess_err ? -EFAULT : 0);	\
} while (0)


#define __copy_to_user copy_to_user
#define __copy_from_user copy_from_user
#define __copy_to_user_inatomic copy_to_user
#define __copy_from_user_inatomic __copy_from_user

unsigned long __must_check __copy_to_user_ll
		(void __user *to, const void *from, unsigned long n);
unsigned long __must_check __copy_from_user_ll
		(void *to, const void __user *from, unsigned long n);

/**
 * Here we special-case 1, 2 and 4-byte copy_*_user invocations.  On a fault
 * we return the initial request size (1, 2 or 4), as copy_*_user should do.
 * If a store crosses a page boundary and gets a fault, the x86 will not write
 * anything, so this is accurate.
 */

unsigned long __must_check copy_to_user(void __user *to,
					const void *from, unsigned long n);
unsigned long __must_check copy_from_user(void *to,
					  const void __user *from,
					  unsigned long n);
long __must_check strncpy_from_user(char *dst, const char __user *src,
				    long count);
long __must_check __strncpy_from_user(char *dst,
				      const char __user *src, long count);

/**
 * __put_user: - Write a simple value into user space, with less checking.
 * @x:   Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 */

unsigned long __must_check clear_user(void __user *mem, unsigned long len);
unsigned long __must_check __clear_user(void __user *mem, unsigned long len);


/*
 * {get|put}_user_try and catch
 *
 * get_user_try {
 *	get_user_ex(...);
 * } get_user_catch(err)
 */
#define get_user_try		uaccess_try
#define get_user_catch(err)	uaccess_catch(err)

#define get_user_ex(x, ptr)	do {					\
	unsigned long __gue_val;					\
	__get_user_size_ex((__gue_val), (ptr), (sizeof(*(ptr))));	\
	(x) = (__force __typeof__(*(ptr)))__gue_val;			\
} while (0)

#define put_user_try		do {		\
	int __uaccess_err = 0;

#define put_user_catch(err)			\
	(err) |= __uaccess_err;			\
} while (0)

#define put_user_ex(x, ptr)	do {		\
	__uaccess_err |= __put_user(x, ptr);	\
} while (0)

extern unsigned long
copy_from_user_nmi(void *to, const void __user *from, unsigned long n);
extern __must_check long
strncpy_from_user(char *dst, const char __user *src, long count);

extern __must_check long strlen_user(const char __user *str);
extern __must_check long strnlen_user(const char __user *str, long n);

unsigned long __must_check clear_user(void __user *mem, unsigned long len);
unsigned long __must_check __clear_user(void __user *mem, unsigned long len);

extern void __cmpxchg_wrong_size(void)
	__compiletime_error("Bad argument size for cmpxchg");

#define __user_atomic_cmpxchg_inatomic(uval, ptr, old, new, size)	\
({									\
	int __ret = 0;							\
	__typeof__(ptr) __uval = (uval);				\
	__typeof__(*(ptr)) __old = (old);				\
	__typeof__(*(ptr)) __new = (new);				\
	switch (size) {							\
	case 1:								\
	{								\
		asm volatile("\t" ASM_STAC "\n"				\
			"1:\t" LOCK_PREFIX "cmpxchgb %4, %2\n"		\
			"2:\t" ASM_CLAC "\n"				\
			"\t.section .fixup, \"ax\"\n"			\
			"3:\tmov     %3, %0\n"				\
			"\tjmp     2b\n"				\
			"\t.previous\n"					\
			_ASM_EXTABLE(1b, 3b)				\
			: "+r" (__ret), "=a" (__old), "+m" (*(ptr))	\
			: "i" (-EFAULT), "q" (__new), "1" (__old)	\
			: "memory"					\
		);							\
		break;							\
	}								\
	case 2:								\
	{								\
		asm volatile("\t" ASM_STAC "\n"				\
			"1:\t" LOCK_PREFIX "cmpxchgw %4, %2\n"		\
			"2:\t" ASM_CLAC "\n"				\
			"\t.section .fixup, \"ax\"\n"			\
			"3:\tmov     %3, %0\n"				\
			"\tjmp     2b\n"				\
			"\t.previous\n"					\
			_ASM_EXTABLE(1b, 3b)				\
			: "+r" (__ret), "=a" (__old), "+m" (*(ptr))	\
			: "i" (-EFAULT), "r" (__new), "1" (__old)	\
			: "memory"					\
		);							\
		break;							\
	}								\
	case 4:								\
	{								\
		asm volatile("\t" ASM_STAC "\n"				\
			"1:\t" LOCK_PREFIX "cmpxchgl %4, %2\n"		\
			"2:\t" ASM_CLAC "\n"				\
			"\t.section .fixup, \"ax\"\n"			\
			"3:\tmov     %3, %0\n"				\
			"\tjmp     2b\n"				\
			"\t.previous\n"					\
			_ASM_EXTABLE(1b, 3b)				\
			: "+r" (__ret), "=a" (__old), "+m" (*(ptr))	\
			: "i" (-EFAULT), "r" (__new), "1" (__old)	\
			: "memory"					\
		);							\
		break;							\
	}								\
	case 8:								\
	{								\
		if (!IS_ENABLED(CONFIG_X86_64))				\
			__cmpxchg_wrong_size();				\
									\
		asm volatile("\t" ASM_STAC "\n"				\
			"1:\t" LOCK_PREFIX "cmpxchgq %4, %2\n"		\
			"2:\t" ASM_CLAC "\n"				\
			"\t.section .fixup, \"ax\"\n"			\
			"3:\tmov     %3, %0\n"				\
			"\tjmp     2b\n"				\
			"\t.previous\n"					\
			_ASM_EXTABLE(1b, 3b)				\
			: "+r" (__ret), "=a" (__old), "+m" (*(ptr))	\
			: "i" (-EFAULT), "r" (__new), "1" (__old)	\
			: "memory"					\
		);							\
		break;							\
	}								\
	default:							\
		__cmpxchg_wrong_size();					\
	}								\
	*__uval = __old;						\
	__ret;								\
})

#define user_atomic_cmpxchg_inatomic(uval, ptr, old, new)		\
({									\
	access_ok(VERIFY_WRITE, (ptr), sizeof(*(ptr))) ?		\
		__user_atomic_cmpxchg_inatomic((uval), (ptr),		\
				(old), (new), sizeof(*(ptr))) :		\
		-EFAULT;						\
})

/*
 * movsl can be slow when source and dest are not both 8-byte aligned
 */
#ifdef CONFIG_X86_INTEL_USERCOPY
extern struct movsl_mask {
	int mask;
} ____cacheline_aligned_in_smp movsl_mask;
#endif

#ifndef CONFIG_L4

#define ARCH_HAS_NOCACHE_UACCESS 1

#ifdef CONFIG_X86_32
# include <asm/uaccess_32.h>
#else
# include <asm/uaccess_64.h>
#endif

unsigned long __must_check _copy_from_user(void *to, const void __user *from,
					   unsigned n);
unsigned long __must_check _copy_to_user(void __user *to, const void *from,
					 unsigned n);

#ifdef CONFIG_DEBUG_STRICT_USER_COPY_CHECKS
# define copy_user_diag __compiletime_error
#else
# define copy_user_diag __compiletime_warning
#endif

extern void copy_user_diag("copy_from_user() buffer size is too small")
copy_from_user_overflow(void);
extern void copy_user_diag("copy_to_user() buffer size is too small")
copy_to_user_overflow(void) __asm__("copy_from_user_overflow");

#undef copy_user_diag

#ifdef CONFIG_DEBUG_STRICT_USER_COPY_CHECKS

extern void
__compiletime_warning("copy_from_user() buffer size is not provably correct")
__copy_from_user_overflow(void) __asm__("copy_from_user_overflow");
#define __copy_from_user_overflow(size, count) __copy_from_user_overflow()

extern void
__compiletime_warning("copy_to_user() buffer size is not provably correct")
__copy_to_user_overflow(void) __asm__("copy_from_user_overflow");
#define __copy_to_user_overflow(size, count) __copy_to_user_overflow()

#else

static inline void
__copy_from_user_overflow(int size, unsigned long count)
{
	WARN(1, "Buffer overflow detected (%d < %lu)!\n", size, count);
}

#define __copy_to_user_overflow __copy_from_user_overflow

#endif

static inline unsigned long __must_check
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	int sz = __compiletime_object_size(to);

	might_fault();

	/*
	 * While we would like to have the compiler do the checking for us
	 * even in the non-constant size case, any false positives there are
	 * a problem (especially when DEBUG_STRICT_USER_COPY_CHECKS, but even
	 * without - the [hopefully] dangerous looking nature of the warning
	 * would make people go look at the respecitive call sites over and
	 * over again just to find that there's no problem).
	 *
	 * And there are cases where it's just not realistic for the compiler
	 * to prove the count to be in range. For example when multiple call
	 * sites of a helper function - perhaps in different source files -
	 * all doing proper range checking, yet the helper function not doing
	 * so again.
	 *
	 * Therefore limit the compile time checking to the constant size
	 * case, and do only runtime checking for non-constant sizes.
	 */

	if (likely(sz < 0 || sz >= n))
		n = _copy_from_user(to, from, n);
	else if(__builtin_constant_p(n))
		copy_from_user_overflow();
	else
		__copy_from_user_overflow(sz, n);

	return n;
}

static inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
	int sz = __compiletime_object_size(from);

	might_fault();

	/* See the comment in copy_from_user() above. */
	if (likely(sz < 0 || sz >= n))
		n = _copy_to_user(to, from, n);
	else if(__builtin_constant_p(n))
		copy_to_user_overflow();
	else
		__copy_to_user_overflow(sz, n);

	return n;
}

#undef __copy_from_user_overflow
#undef __copy_to_user_overflow

#else

#ifdef CONFIG_X86_64
__must_check int
__copy_in_user(void __user *dst, const void __user *src, unsigned size);

__must_check unsigned long
copy_in_user(void __user *to, const void __user *from, unsigned len);
#endif /* X86_64 */

#endif /* L4 */

#endif

