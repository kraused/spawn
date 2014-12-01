
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "network.h"


int network_ctor(struct network *self, struct alloc *alloc)
{
	memset(self, 0, sizeof(*self));

	self->alloc    = alloc;
	self->listenfd = -1;
	self->newfd    = -1;

	return 0;
}

int network_dtor(struct network *self)
{
	memset(self, 0, sizeof(*self));

	return 0;
}

int network_lock_acquire(struct network *self)
{
	int err;

	err = lock_acquire(&self->lock);
	if (unlikely(err)) {
		error("lock_acquire() failed with error %d.", err);
		return err;
	}

	return 0;
}

int network_lock_release(struct network *self)
{
	int err;

	err = lock_release(&self->lock);
	if (unlikely(err)) {
		error("lock_release() failed with error %d.", err);
		return err;
	}

	return 0;
}

int network_resize(struct network *self, int size)
{
	int err;
	int i;

	if (unlikely(self->size > 0)) {
		/* FIXME Reallocating the memory is not a problem but what should
		 *       we do with the lft content? */
		error("network_resize() does not support this case yet.");
		return -ENOTIMPL;
	}

	err = ZALLOC(self->alloc, (void **)&self->lft, size,
	             sizeof(si32), "lft");
	if (unlikely(err)) {
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	for (i = 0; i < size; ++i)
		self->lft[i] = -1;

	return 0;
}

int network_add_ports(struct network *self, int *fds, int nfds)
{
	int err;
	int i;

	err = REALLOC(self->alloc, (void **)&self->ports,
	              self->nports, sizeof(int),
	              (self->nports + nfds), sizeof(int),
	              "ports");
	if (unlikely(err)) {
		error("REALLOC() failed with error %d.", err);
		return err;
	}

	for (i = 0; i < nfds; ++i)
		self->ports[self->nports + i] = fds[i];

	self->nports += nfds;

	return 0;
}

int network_initialize_lft(struct network *self, int port)
{
	si32 i;

	if (unlikely(!self || (port < 0) || (port >= self->nports)))
		return -EINVAL;

	for (i = 0; i < self->size; ++i)
		self->lft[i] = port;

	return 0;
}

int network_modify_lft(struct network *self, int port, si32 *ids, si32 nids)
{
	si32 i;

	if (unlikely(!self || !ids ||
	             (nids < 0) || (nids > self->size) ||
	             (port < 0) || (port >= self->nports)))
		return -EINVAL;

	for (i = 0; i < nids; ++i) {
		if (unlikely((ids[i] < 0) || (ids[i] >= self->size)))
			return -EINVAL;

		self->lft[ids[i]] = port;
	}

	return 0;
}
