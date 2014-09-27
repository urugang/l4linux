#include <l4/cxx/ipc_server>
#include <l4/log/log.h>
#include <l4/re/event>
#include <l4/libfdx/fdx>

#include <l4/re/util/event_buffer>
#include <l4/re/util/event_svr>
#include <l4/re/util/meta>

#include <asm/server/server.h>
#include <asm/server/fdx-srv.h>
#include <asm/server/util.h>

#include <l4/sys/ktrace.h>

#include <cassert>
#include <stdlib.h>

#include <l4/sys/factory>

class Fdx_factory : public l4x_srv_object_tmpl<Fdx_factory>
{
public:
	explicit Fdx_factory(l4x_fdx_srv_factory_ops *ops)
	: ops(ops)
	{}

	int dispatch(l4_umword_t obj, L4::Ipc::Iostream &ios);

	void *operator new(size_t size, void *p)
	{
		assert(size == sizeof(Fdx_factory));
		return p;
	}

private:
	l4x_fdx_srv_factory_ops *ops;

	int dispatch_factory(L4::Ipc::Iostream &ios);
	int get_unsigned(L4::Ipc::Varg o, const char *param, unsigned &result);
};

int
Fdx_factory::dispatch(l4_umword_t, L4::Ipc::Iostream &ios)
{
	l4_msgtag_t tag;
	ios >> tag;

	switch (tag.label()) {
		case L4::Meta::Protocol:
			return L4::Util::handle_meta_request<L4::Factory>(ios);
		case L4::Factory::Protocol:
			return dispatch_factory(ios);
		default:
			return -L4_EBADPROTO;
	}
}

int Fdx_factory::get_unsigned(L4::Ipc::Varg o, const char *param,
                              unsigned &result)
{
	char buf[12];
	int param_len = strlen(param);

	if (o.length() > param_len
	    && !strncmp(o.value<char const *>(), param, param_len)) {
		unsigned l = o.length() - param_len;
		memcpy(buf, o.value<char const *>() + param_len, l);
		buf[l] = 0;
		result = strtoul(buf, 0, 0);
		return 1;
	}
	return 0;
}

int Fdx_factory::dispatch_factory(L4::Ipc::Iostream &ios)
{
	L4::Opcode op;
	ios >> op;

	// ignore op value

	l4x_srv_factory_create_data data;
	data.opt_flags = 0;

	L4::Ipc::Varg o;
	while (ios.get(&o)) {
		if (!o.is_of<char const *>()) {
			LOG_printf("blk-srv: Invalid option\n");
			continue;
		}

		if (get_unsigned(o, "uid=", data.uid)) {
			data.opt_flags |= L4X_FDX_SRV_FACTORY_HAS_UID;
		} else if (get_unsigned(o, "gid=", data.gid)) {
			data.opt_flags |= L4X_FDX_SRV_FACTORY_HAS_GID;
		} else if (get_unsigned(o, "openflags_mask=", data.openflags_mask)) {
			data.opt_flags |= L4X_FDX_SRV_FACTORY_HAS_OPENFLAGS_MASK;
		} else if (o.length() == 6
		           && !strncmp(o.value<char const *>(), "nogrow", 6)) {
			data.opt_flags |= L4X_FDX_SRV_FACTORY_HAS_FLAG_NOGROW;
		} else if (o.length() > 9
		           && !strncmp(o.value<char const *>(), "basepath=", 9)) {
			data.basepath_len = o.length() - 9;
			data.basepath = o.value<char const *>() + 9;
			data.opt_flags |= L4X_FDX_SRV_FACTORY_HAS_BASEPATH;
		} else if (o.length() > 11
		           && !strncmp(o.value<char const *>(), "filterpath=", 11)) {
			data.filterpath_len = o.length() - 11;
			data.filterpath = o.value<char const *>() + 11;
			data.opt_flags |= L4X_FDX_SRV_FACTORY_HAS_FILTERPATH;
		} else {
			LOG_printf("blk-srv: Unknown option '%.*s'\n",
			           o.length(), o.value<char const *>());
		}
	}

	l4_cap_idx_t client_cap;
	int r = ops->create(&data, &client_cap);
	if (r < 0)
		return r;

	L4::Cap<void> objcap(client_cap);
	ios << objcap;

	return 0;
}

C_FUNC int
l4x_fdx_factory_create(l4_cap_idx_t thread,
                       struct l4x_fdx_srv_factory_ops *ops,
                       void *objmem)
{

	Fdx_factory *fact = new (objmem) Fdx_factory(ops);

	L4::Cap<void> c;
	c = l4x_srv_register_name(fact, L4::Cap<L4::Thread>(thread), "fdx");
	if (!c) {
		LOG_printf("l4x-fdx-srv: 'fdx' object registration failed.\n");
		return -L4_ENODEV;
	}

	return 0;
}

C_FUNC unsigned
l4x_fdx_factory_objsize()
{
	return sizeof(Fdx_factory);
}



class Fdx_server : public L4Re::Util::Event_svr<Fdx_server>,
                   public l4x_srv_object_tmpl<Fdx_server>,
                   public l4fdx_srv_struct
{
public:
	l4x_fdx_srv_ops *ops;

	enum Opcodes {
		Op_open         = 0,
		Op_close        = 1,
		Op_read         = 2,
		Op_write        = 3,
		Op_getshm_read  = 4,
		Op_getshm_write = 5,
		Op_fstat        = 6,
		Op_ping         = 7,
	};

	enum {
		Size_shm_read  = L4_PAGESIZE * 16,
		Size_shm_write = L4_PAGESIZE * 16,
	};

	int init();
	int dispatch(l4_umword_t obj, L4::Ipc::Iostream &ios);

	void reset_event_buffer() {}

	void *shm_base_read() const { return _shm_base_read; }
	void *shm_base_write() const { return _shm_base_write; }
	void add(struct l4x_fdx_srv_result_t *);
	void trigger() const
	{
		if (l4_ipc_error(_irq.cap()->trigger(), l4_utcb()))
			LOG_printf("TRIGGER FAILED\n");
	}
	L4::Cap<L4::Kobject> rcv_cap() { return l4x_srv_rcv_cap(); }

	l4x_fdx_srv_data srv_data;

	void *operator new(size_t size, void *p)
	{
		assert(size == sizeof(Fdx_server));
		return p;
	}

private:
	int dispatch_fdx(l4_umword_t obj, L4::Ipc::Iostream &ios);

	int dispatch_open(l4_umword_t obj, L4::Ipc::Iostream &ios);
	int dispatch_read(l4_umword_t obj, L4::Ipc::Iostream &ios);
	int dispatch_write(l4_umword_t obj, L4::Ipc::Iostream &ios);
	int dispatch_close(l4_umword_t obj, L4::Ipc::Iostream &ios);
	int dispatch_getshm_read(l4_umword_t obj, L4::Ipc::Iostream &ios);
	int dispatch_getshm_write(l4_umword_t obj, L4::Ipc::Iostream &ios);
	int dispatch_fstat(l4_umword_t obj, L4::Ipc::Iostream &ios);

	int alloc_shm(L4::Cap<L4Re::Dataspace> *ds,
                      void **addr, unsigned size);
	void free_shm(void *addr);

	L4Re::Util::Event_buffer_t<l4x_fdx_srv_result_payload_t> _evbuf;
	L4::Cap<L4Re::Dataspace> _shm_ds_read, _shm_ds_write;
	void *_shm_base_read;
	void *_shm_base_write;

};

int
Fdx_server::alloc_shm(L4::Cap<L4Re::Dataspace> *ds,
                      void **addr, unsigned size)
{
	L4Re::Util::Auto_cap<L4Re::Dataspace>::Cap d
		= L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>();
	if (!d.is_valid())
		return -L4_ENOMEM;

	int r = L4Re::Env::env()->mem_alloc()->alloc(size, d.get());
	if (r < 0)
		return r;

	*addr = 0;
	r = L4Re::Env::env()->rm()->attach(addr, size,
	                                   L4Re::Rm::Search_addr, d.get());
	if (r < 0) {
		L4Re::Env::env()->mem_alloc()->free(d.get());
		return r;
	}

	*ds = d.release();
	return 0;
}

void
Fdx_server::free_shm(void *addr)
{
	L4::Cap<L4Re::Dataspace> ds;

	L4Re::Env::env()->rm()->detach(addr, &ds);
	L4Re::Env::env()->mem_alloc()->free(ds);
}

int
Fdx_server::init()
{
	int r = L4x_server_util::get_event_buffer(L4_PAGESIZE,
	                                          &_evbuf, &_ds);
	if (r)
		return r;

	r = alloc_shm(&_shm_ds_read, &_shm_base_read, Size_shm_read);
	if (r)
		goto out_free_ev;

	r = alloc_shm(&_shm_ds_write, &_shm_base_write, Size_shm_write);
	if (r)
		goto out_free_read_shm;

	static_assert(sizeof(Fdx_conn::Result_payload)
	              == sizeof(l4x_fdx_srv_result_payload_t),
	              "Fdx_conn::Result_payload vs."
		      " l4x_fdx_srv_result_payload_t size mismatch");

	static_assert(sizeof(L4Re::Event_buffer_t<Fdx_conn::Result_payload>::Event)
	              == sizeof(l4x_fdx_srv_result_t),
	              "Fdx_conn::Request_buf vs."
		      " l4x_fdx_srv_result_t size mismatch");

	return 0;

out_free_read_shm:
	free_shm(_shm_base_read);
out_free_ev:
	L4x_server_util::free_event_buffer(&_evbuf, _ds);
	return r;
}

int
Fdx_server::dispatch_open(l4_umword_t, L4::Ipc::Iostream &ios)
{
	char *p = 0;
	unsigned long len;
	unsigned mode;
	int flags;

	ios >> L4::Ipc::buf_in(p, len) >> flags >> mode;

	int i = ops->open(this, p, len, flags, mode);
	if (i < 0)
		return i;
	ios << i;

	return L4_EOK;
}

int
Fdx_server::dispatch_close(l4_umword_t, L4::Ipc::Iostream &ios)
{
	unsigned fid;
	ios >> fid;
	return ops->close(this, fid);
}

int
Fdx_server::dispatch_getshm_read(l4_umword_t, L4::Ipc::Iostream &ios)
{
	ios << _shm_ds_read;
	return L4_EOK;
}

int
Fdx_server::dispatch_getshm_write(l4_umword_t, L4::Ipc::Iostream &ios)
{
	ios << _shm_ds_write;
	return L4_EOK;
}

int
Fdx_server::dispatch_read(l4_umword_t, L4::Ipc::Iostream &ios)
{
	unsigned fid, size;
	l4_uint64_t start;
	ios >> fid >> start >> size;
	return ops->read(this, fid, start, size);
}

int
Fdx_server::dispatch_write(l4_umword_t, L4::Ipc::Iostream &ios)
{
	unsigned fid, shm_offset, size;
	l4_uint64_t start;
	ios >> fid >> start >> size >> shm_offset;
	return ops->write(this, fid, start, size, shm_offset);
}

int
Fdx_server::dispatch_fstat(l4_umword_t, L4::Ipc::Iostream &ios)
{
	unsigned fid;
	ios >> fid;
	return ops->fstat(this, fid);
}

int
Fdx_server::dispatch_fdx(l4_umword_t obj, L4::Ipc::Iostream &ios)
{
	L4::Opcode op;

	ios >> op;

	switch (op) {
		case Op_open: return dispatch_open(obj, ios);
		case Op_read: return dispatch_read(obj, ios);
		case Op_write: return dispatch_write(obj, ios);
		case Op_close: return dispatch_close(obj, ios);
		case Op_getshm_read: return dispatch_getshm_read(obj, ios);
		case Op_getshm_write: return dispatch_getshm_write(obj, ios);
		case Op_fstat: return dispatch_fstat(obj, ios);
		case Op_ping: return 0; break;
		default:
			return -L4_ENOSYS;
	};
}

int
Fdx_server::dispatch(l4_umword_t obj, L4::Ipc::Iostream &ios)
{
	l4_msgtag_t tag;
	ios >> tag;

	switch (tag.label()) {
		case L4::Meta::Protocol:
			return L4::Util::handle_meta_request<L4Re::Event>(ios);
		case L4Re::Protocol::Event:
		case L4::Protocol::Irq:
			return L4Re::Util::Event_svr<Fdx_server>::dispatch(obj, ios);
		case 19:
			return dispatch_fdx(obj, ios);
		default:
			LOG_printf("unknown proto %ld\n", tag.label());
			return -L4_EBADPROTO;
	}

	return -L4_EBADPROTO;
}

void
Fdx_server::add(struct l4x_fdx_srv_result_t *e)
{
	// check return value
	_evbuf.put(*reinterpret_cast<L4Re::Event_buffer_t<l4x_fdx_srv_result_payload_t>::Event const *>(e));
}

static l4fdx_srv_obj
__l4x_fdx_srv_create(l4_cap_idx_t thread, struct l4x_fdx_srv_ops *ops,
                     struct l4fdx_client *client, void *objmem,
                     l4_cap_idx_t *client_cap, const char *capname)
{
	int err;

	Fdx_server *fdx = new (objmem) Fdx_server();

	if ((err = fdx->init()) < 0) {
		LOG_printf("l4x-fdx-srv: Initialization failed (%d)\n", err);
		return (l4fdx_srv_obj)err;
	}

	fdx->ops = ops;

	fdx->srv_data.shm_base_read  = fdx->shm_base_read();
	fdx->srv_data.shm_base_write = fdx->shm_base_write();
	fdx->srv_data.shm_size_read  = fdx->Size_shm_read;
	fdx->srv_data.shm_size_write = fdx->Size_shm_write;

	fdx->client = client;

	L4::Cap<void> c;

	if (capname)
		c = l4x_srv_register_name(fdx, L4::Cap<L4::Thread>(thread),
		                          capname);
	else
		c = l4x_srv_register(fdx, L4::Cap<L4::Thread>(thread));
	if (!c) {
		LOG_printf("l4x-fdx-srv: Object registration failed.\n");
		return (l4fdx_srv_obj)-L4_ENODEV;
	}

	if (client_cap)
		*client_cap = c.cap();

	return fdx;
}

extern "C" l4fdx_srv_obj
l4x_fdx_srv_create_name(l4_cap_idx_t thread, const char *capname,
                        struct l4x_fdx_srv_ops *ops,
                        struct l4fdx_client *client,
                        void *objmem)
{
	return __l4x_fdx_srv_create(thread, ops, client, objmem,
	                            0, capname);

}

extern "C" l4fdx_srv_obj
l4x_fdx_srv_create(l4_cap_idx_t thread,
                   struct l4x_fdx_srv_ops *ops,
                   struct l4fdx_client *client,
                   void *objmem,
                   l4_cap_idx_t *client_cap)
{
	return __l4x_fdx_srv_create(thread, ops, client, objmem,
	                            client_cap, 0);
}

C_FUNC unsigned
l4x_fdx_srv_objsize()
{
	return sizeof(Fdx_server);
}

static inline Fdx_server *cast_to(l4fdx_srv_obj o)
{
	return static_cast<Fdx_server *>(o);
}

C_FUNC void
l4x_fdx_srv_add_event(l4fdx_srv_obj fdxobjp, struct l4x_fdx_srv_result_t *e)
{
	cast_to(fdxobjp)->add(e);
}

C_FUNC void
l4x_fdx_srv_trigger(l4fdx_srv_obj fdxobjp)
{
	cast_to(fdxobjp)->trigger();
}

C_FUNC struct l4x_fdx_srv_data *
l4x_fdx_srv_get_srv_data(l4fdx_srv_obj fdxobjp)
{
	return &cast_to(fdxobjp)->srv_data;
}
