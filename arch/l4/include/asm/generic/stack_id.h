#ifndef __ASM_L4__GENERIC__STACK_ID_H__
#define __ASM_L4__GENERIC__STACK_ID_H__

#include <asm/thread_info.h>
#include <asm/l4lxapi/thread.h>
#include <l4/sys/utcb.h>
#ifdef CONFIG_L4_VCPU
#include <l4/sys/vcpu.h>
#endif

struct l4x_stack_struct {
	l4_utcb_t *l4utcb;
};

enum {
	L4X_UTCB_TCR_ID = 0,
	L4X_UTCB_TCR_PRIO = 1,
};

static inline
struct l4x_stack_struct *l4x_stack_struct_get(struct thread_info *ti)
{
	/* struct is just after the thread_info struct on the stack */
	return (struct l4x_stack_struct *)(ti + 1);
}

static inline l4_utcb_t *l4x_stack_utcb_get(void)
{
	return l4x_stack_struct_get(current_thread_info())->l4utcb;
}

#ifdef CONFIG_L4_VCPU
static inline l4_vcpu_state_t *l4x_stack_vcpu_state_get(void)
{
	return (l4_vcpu_state_t *)((char *)l4x_stack_utcb_get()
	                           + L4_UTCB_OFFSET);
}
#endif

static inline void l4x_stack_setup(struct thread_info *ti)
{
	struct l4x_stack_struct *s = l4x_stack_struct_get(ti);
	s->l4utcb = l4_utcb();
}

static inline l4_cap_idx_t l4x_stack_id_get(void)
{
	return l4_utcb_tcr_u(l4x_stack_utcb_get())->user[L4X_UTCB_TCR_ID];
}

static inline void l4x_stack_id_set(struct thread_info *ti,
                                    l4_cap_idx_t id)
{
	l4_utcb_t *u = l4x_stack_struct_get(ti)->l4utcb;
	l4_utcb_tcr_u(u)->user[L4X_UTCB_TCR_ID] = id;
}

#ifndef CONFIG_L4_VCPU
static inline unsigned int l4x_stack_prio_get(void)
{
	return l4_utcb_tcr_u(l4x_stack_utcb_get())->user[L4X_UTCB_TCR_PRIO];
}
#endif

#endif /* ! __ASM_L4__GENERIC__STACK_ID_H__ */
