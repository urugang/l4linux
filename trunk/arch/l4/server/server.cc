#include <l4/sys/capability>
#include <l4/sys/typeinfo_svr>
#include <l4/sys/ipc_gate>
#include <l4/cxx/ipc_stream>
#include <l4/cxx/thread>
#include <l4/re/env>
#include <l4/re/util/cap_alloc>

#include <l4/log/log.h>

#include <asm/server/server.h>

L4::Cap<void>
l4x_srv_register_name(l4x_srv_object *o,
                      L4::Cap<L4::Thread> thread, const char *service)
{
	L4::Cap<L4::Ipc_gate> cap
		= L4Re::Env::env()->get_cap<L4::Ipc_gate>(service);
	if (!cap)
		return cap;

	int err = l4_error(cap->bind_thread(thread, (l4_umword_t)o));
	if (err < 0)
		return L4::Cap<void>::Invalid;

	return cap;
}

C_FUNC l4_cap_idx_t
l4x_srv_register_name_c(struct l4x_srv_object *obj,
                        l4_cap_idx_t thread, const char *service)
{
	return l4x_srv_register_name(obj, L4::Cap<L4::Thread>(thread),
	                             service).cap();
}

static L4::Cap<L4::Kobject> _rcv_cap;

L4::Cap<L4::Kobject> l4x_srv_rcv_cap()
{
	return _rcv_cap;
}

C_FUNC void
l4x_srv_init(void)
{
	_rcv_cap = L4Re::Util::cap_alloc.alloc<L4::Kobject>();
	if (!_rcv_cap)
		LOG_printf("l4x_srv: rcv-cap alloc failed\n");
}

C_FUNC void
l4x_srv_setup_recv(l4_utcb_t *u)
{
	l4_utcb_br_u(u)->br[0] = _rcv_cap.cap() | L4_RCV_ITEM_SINGLE_CAP
	                         | L4_RCV_ITEM_LOCAL_ID;
	l4_utcb_br_u(u)->bdr = 0;
}
