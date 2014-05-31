#pragma once

#include <l4/re/util/event_buffer>

namespace L4x_server_util
{

template< typename T >
int
get_event_buffer(unsigned size, L4Re::Util::Event_buffer_t<T> *evbuf,
                 L4::Cap<L4Re::Dataspace> *ds)
{
	L4Re::Util::Auto_cap<L4Re::Dataspace>::Cap b
		= L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>();
	if (!b.is_valid())
		return -L4_ENOMEM;

	int r;
	if ((r = L4Re::Env::env()->mem_alloc()->alloc(size, b.get())) < 0)
		return r;

	if ((r = evbuf->attach(b.get(), L4Re::Env::env()->rm())) < 0) {
		L4Re::Env::env()->mem_alloc()->free(b.get());
		return r;
	}

	memset(evbuf->buf(), 0, b.get()->size());

	*ds = b.release();

	return 0;
}

template< typename T >
int
free_event_buffer(L4Re::Util::Event_buffer_t<T> *evbuf,
                  L4::Cap<L4Re::Dataspace> ds)
{
	int r = evbuf->detach(L4Re::Env::env()->rm());
	if (r)
		return r;

	return L4Re::Env::env()->mem_alloc()->free(ds);
}

}
