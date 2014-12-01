
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/un.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "spawn.h"
#include "plugin.h"
#include "helper.h"


static int _copy_hosts(struct spawn *self, int nhosts, const char **hosts);
static int _free_hosts(struct spawn* self);
static int _alloc_procs(struct spawn *self, int nprocs);
static int _free_procs(struct spawn* self);
static int _setup_tree(struct network *self, struct alloc *alloc,
                       int size, int here);
static int _tree_bind_listenfd(struct network *self, struct sockaddr *addr,
                               ull addrlen);
static int _tree_listen(struct network *self, int backlog);
static int _tree_close_listenfd(struct network *self);


int spawn_ctor(struct spawn *self, struct alloc *alloc)
{
	int err;

	memset(self, 0, sizeof(*self));

	self->alloc = alloc;

	err = network_ctor(&self->tree, self->alloc);
	if (unlikely(err)) {
		error("struct network constructor failed with error %d.", err);
		return err;
	}

	err = buffer_pool_ctor(&self->bufpool,
	                       self->alloc, 128);	/* FIXME Make this configurable. */
	if (unlikely(err)) {
		error("struct buffer_pool constructor failed with error %d.", err);
		return err;
	}

	err = comm_ctor(&self->comm, self->alloc,
	                &self->tree, &self->bufpool,
			128, 128);			/* FIXME Make these configurable. */
	if (unlikely(err)) {
		error("struct comm constructor failed with error %d.", err);
		return err;
	}

	return 0;
}

int spawn_dtor(struct spawn *self)
{
	int err;

	err = comm_dtor(&self->comm);
	if (unlikely(err)) {
		error("struct comm destructor failed with error %d.", err);
		return err;
	}

	err = buffer_pool_dtor(&self->bufpool);
	if (unlikely(err)) {
		error("struct buffer_pool destructor failed with error %d.", err);
		return err;
	}

	err = network_dtor(&self->tree);
	if (unlikely(err)) {
		error("struct network destructor failed with error %d.", err);
		return err;
	}

	memset(self, 0, sizeof(*self));

	return 0;
}

int spawn_setup_on_local(struct spawn *self, int nhosts,
                         const char **hosts, int treewidth)
{
	int err;

	if (unlikely((nhosts < 0) || !hosts || (treewidth < 0)))
		return -EINVAL;

	err = _copy_hosts(self, nhosts, hosts);
	if (unlikely(err))
		return err;

	err = _alloc_procs(self, treewidth);
	if (unlikely(err))
		goto fail1;

	err = _setup_tree(&self->tree, self->alloc, nhosts, 0);
	if (unlikely(err))
		goto fail2;

	return 0;

fail2:
	assert(err);

	_free_procs(self);	/* Ignore error. _free_procs() will report
	                  	 * problems using error(). */
fail1:
	assert(err);

	_free_hosts(self);

	return err;
}

int spawn_setup_on_other(struct spawn *self, int nhosts, int here)
{
	int err;

	err = _setup_tree(&self->tree, self->alloc, nhosts, here);
	if (unlikely(err))
		return err;

	return 0;
}

int spawn_load_exec_plugin(struct spawn *self, const char *name)
{
	struct plugin *plu;

	if (unlikely(self->exec)) {
		warn("self->exec is not NULL. This may cause a memory leak.");
		self->exec = NULL;
	}

	plu = load_plugin(name);
	if (unlikely(!plu))
		return -ESOMEFAULT;	/* load_plugin() reported reason. */

	self->exec = cast_to_exec_plugin(plu);
	if (unlikely(!self->exec)) {
		error("Plugin '%s' is not an exec plugin.", name);
		return -EINVAL;
	}

	return 0;
}

int spawn_bind_listenfd(struct spawn *self, struct sockaddr *addr, ull addrlen)
{
	return _tree_bind_listenfd(&self->tree, addr, addrlen);
}

int spawn_comm_start(struct spawn *self)
{
	int err;

	err = _tree_listen(&self->tree, 8);	/* FIXME Make the backlog
						 *       configurable.
						 */
	if (unlikely(err))
		return err;

	err = comm_start_processing(&self->comm);
	if (unlikely(err))
		return err;

	return 0;
}

int spawn_comm_halt(struct spawn *self)
{
	int err;

	err = comm_halt_processing(&self->comm);
	if (unlikely(err))
		return err;

	err = _tree_close_listenfd(&self->tree);
	if (unlikely(err))
		return err;

	return 0;
}


static int _copy_hosts(struct spawn *self, int nhosts, const char **hosts)
{
	int err;

	/* FIXME Warn if the input is not ok. */

	err = array_of_str_dup(self->alloc, nhosts, hosts, (char ***)&self->hosts);
	if (unlikely(err))
		return err;

	self->nhosts = nhosts;

	return 0;
}

static int _free_hosts(struct spawn* self)
{
	int err;
	int nhosts = self->nhosts;

	self->nhosts = 0;

	err = array_of_str_free(self->alloc, nhosts, (char ***)&self->hosts);
	if (unlikely(err))
		return err;

	return 0;
}

static int _alloc_procs(struct spawn *self, int nprocs)
{
	int err;

	err = ZALLOC(self->alloc, (void **)&self->procs, nprocs,
	             sizeof(struct process), "procs");
	if (unlikely(err)) {
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	self->nprocs = nprocs;

	return 0;
}

static int _free_procs(struct spawn* self)
{
	int err;
	int nprocs = self->nprocs;

	self->nprocs = 0;

	err = ZFREE(self->alloc, (void **)&self->procs, nprocs,
	            sizeof(struct process), "procs");
	if (unlikely(err)) {
		error("ZFREE() failed with error %d.", err);
		return err;
	}

	return 0;
}

static int _setup_tree(struct network *self, struct alloc *alloc,
                       int size, int here)
{
	int err, tmp;
	int i;

	if (unlikely(size < 0 || here < 0 || here >= size))
		return -EINVAL;

	err = network_resize(self, size);
	if (unlikely(err))
		return err;

	self->here = here;

	return 0;
}

static int _tree_bind_listenfd(struct network *self, struct sockaddr *addr,
                               ull addrlen)
{
	int fd;
	int err;

	if (unlikely(-1 != self->listenfd)) {
		error("File descriptor is valid on entry of spawn_start_listen().");
		return -ESOMEFAULT;
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (unlikely(fd < 0)) {
		error("socket() failed. errno = %d says '%s'.", errno, strerror(errno));
		return -errno;
	}

	err = bind(fd, addr, addrlen);
	if (unlikely(err)) {
		error("bind() failed. errno = %d says '%s'.", errno, strerror(errno));
		close(fd);
		return -errno;
	}

	self->listenfd = fd;

	return 0;
}

static int _tree_listen(struct network *self, int backlog)
{
	int err;

	err = listen(self->listenfd, backlog);
	if (unlikely(err)) {
		error("listen() failed. errno = %d says '%s'.", errno, strerror(errno));
		return -errno;
	}

	return 0;
}

static int _tree_close_listenfd(struct network *self)
{
	int err;

	if (unlikely(-1 == self->listenfd))
		return -ESOMEFAULT;

	err = do_close(self->listenfd);
	if (unlikely(err))
		return err;	/* do_close() reported reason. */

	self->listenfd = -1;

	return 0;
}

