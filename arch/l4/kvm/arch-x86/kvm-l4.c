

#include <linux/module.h>
#include <linux/kvm_host.h>

#include <asm/l4lxapi/task.h>
#include <asm/generic/vcpu.h>

#include <l4/sys/factory.h>
#include <l4/sys/debugger.h>
#include <l4/sys/vm.h>
#include <l4/re/env.h>

int l4x_kvm_create_task(struct kvm *kvm)
{
	l4_msgtag_t t;
	l4_utcb_t *u = l4_utcb();
	int r;
	L4XV_V(f);

	kvm->arch.l4vmcap = L4_INVALID_CAP;

	if (l4lx_task_get_new_task(L4_INVALID_CAP, &kvm->arch.l4vmcap)) {
		printk("%s: could not allocate task cap\n", __func__);
		return -ENOENT;
	}

	L4XV_L(f);
	t = l4_factory_create_vm_u(l4re_env()->factory, kvm->arch.l4vmcap, u);
	if (unlikely((r = l4_error_u(t, u)))) {
		printk("%s: kvm task creation failed cap=%08lx: %d\n",
		       __func__, kvm->arch.l4vmcap, r);
		l4lx_task_number_free(kvm->arch.l4vmcap);
		L4XV_U(f);
		return -ENOENT;
	}
	L4XV_U(f);

	printk("%s: cap = %08lx\n", __func__, kvm->arch.l4vmcap);
#ifdef CONFIG_L4_DEBUG_REGISTER_NAMES
	L4XV_L(f);
	l4_debugger_set_object_name(kvm->arch.l4vmcap, "kvmVM");
	L4XV_U(f);
#endif
	return 0;
}
EXPORT_SYMBOL(l4x_kvm_create_task);

int l4x_kvm_svm_run(l4_cap_idx_t task, l4_fpage_t vmcb_fp,
                    struct l4_vm_svm_gpregs *gpregs)
{
	int r;
	L4XV_V(f);

	L4XV_L(f);
	if ((r = l4_error(l4_vm_run_svm(task, vmcb_fp, gpregs)))) {
		printk("%s: vm run failed with %d\n", __func__, r);
		L4XV_U(f);
		return 1;
	}
	L4XV_U(f);
	return 0;
}
EXPORT_SYMBOL(l4x_kvm_svm_run);


int l4x_kvm_destroy_task(struct kvm *kvm)
{
	printk("%s: cap = %08lx\n", __func__, kvm->arch.l4vmcap);
	if (!l4lx_task_delete_task(kvm->arch.l4vmcap, 1)) {
		printk("%s: kvm task destruction failed cap=%08lx\n",
		       __func__, kvm->arch.l4vmcap);
		l4lx_task_number_free(kvm->arch.l4vmcap);
		return -ENOENT;
	}

	l4lx_task_number_free(kvm->arch.l4vmcap);
	return 0;
}
EXPORT_SYMBOL(l4x_kvm_destroy_task);
