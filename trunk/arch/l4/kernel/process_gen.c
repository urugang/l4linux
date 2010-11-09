
#include <linux/errno.h>

#include <asm/generic/task.h>
#include <asm/generic/hybrid.h>

#include <asm/api/macros.h>

#include <asm/mmu_context.h>
#include <asm/l4lxapi/task.h>
#include <asm/l4lxapi/thread.h>

#include <l4/log/log.h>
#include <l4/sys/debugger.h>

#include <l4/sys/ipc.h>

static void l4x_task_delete(struct mm_struct *mm)
{
	l4_cap_idx_t task_id;

	if (!mm || !mm->context.task ||
	    l4_is_invalid_cap(task_id = mm->context.task))
		return;

	if (!l4lx_task_delete_task(task_id, 0))
		do_exit(9);

	mm->context.task = L4_INVALID_CAP;

	l4lx_task_number_free(task_id);
}

void destroy_context(struct mm_struct *mm)
{
	destroy_context_origarch(mm);
	l4x_task_delete(mm);
}

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context.task = L4_INVALID_CAP;
	return init_new_context_origarch(tsk, mm);
}


void l4x_exit_thread(void)
{
#ifndef CONFIG_L4_VCPU
	int ret;
	int i;

	for (i = 0; i < NR_CPUS; i++) {
		l4_cap_idx_t thread_id = current->thread.user_thread_ids[i];

		/* check if we were a non-user thread (i.e., have no
		   user-space partner) */
		if (unlikely(l4_is_invalid_cap(thread_id)))
			continue;

#ifdef DEBUG
		LOG_printf("exit_thread: trying to delete %s(%d, " PRINTF_L4TASK_FORM ")\n",
		           current->comm, current->pid, PRINTF_L4TASK_ARG(thread_id));
#endif

		/* If task_delete fails we don't free the task number so that it
		 * won't be used again. */

		if (likely(ret = l4lx_task_delete_thread(thread_id))) {
			l4x_hybrid_remove(current);
			current->thread.user_thread_ids[i] = L4_INVALID_CAP;
			l4lx_task_number_free(thread_id);
			current->thread.started = 0;
		} else
			printk("%s: failed to delete task " PRINTF_L4TASK_FORM "\n",
			       __func__, PRINTF_L4TASK_ARG(thread_id));

	}
#endif

#ifdef CONFIG_X86_DS
	ds_exit_thread(current);
#endif
}
