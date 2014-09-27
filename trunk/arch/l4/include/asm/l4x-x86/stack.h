#pragma once

#include <asm/thread_info.h>

static inline struct thread_info *current_thread_info_stack(void)
{
#ifdef CONFIG_X86_64
	register unsigned long sp asm("rsp");
#else
	register unsigned long sp asm("sp");
#endif
	return (struct thread_info *)(sp & ~(THREAD_SIZE - 1));
}
