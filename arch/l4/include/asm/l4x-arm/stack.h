#pragma once

#include <asm/thread_info.h>

static inline struct thread_info *current_thread_info_stack(void)
{
	return current_thread_info();
}
