#ifndef __KVM_L4_H
#define __KVM_L4_H

#include <linux/kvm_host.h>

#include <l4/sys/types.h>

#define L4X_VMCB_LOG2_SIZE 12

int l4x_kvm_create_task(struct kvm *kvm);
int l4x_kvm_destroy_task(struct kvm *kvm);

static inline int l4x_kvm_dbg(void)
{
	return 0;
}

#endif
