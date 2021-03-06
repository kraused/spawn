

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
#include "protocol.h"
#include "options.h"


static int _copy_hosts(struct spawn *self, struct optpool *opts);
static int _count_hosts(const char *hosts);
static int _copy_up_to_char(const char *istr, char *ostr, int len, char x);
static int _free_hosts(struct spawn* self);
static int _setup_tree(struct network *self, struct alloc *alloc,
                       int size, int here);
static int _tree_close_listenfd(struct network *self);


int spawn_ctor(struct spawn *self, struct alloc *alloc, struct optpool *opts,
               int parent, int here)
{
	int err;
	int bufpoolsz, sendqsz, recvqsz;

	memset(self, 0, sizeof(*self));

	self->alloc  = alloc;
	self->opts   = opts;
	self->parent = parent;

	err = _copy_hosts(self, opts);
	if (unlikely(err))
		return err;

	err = hostinfo_ctor(&self->hostinfo, self->alloc);
	if (unlikely(err)) {
		error("struct hostinfo constructor failed with error %d.", err);
		goto fail;
	}

	err = network_ctor(&self->tree, self->alloc);
	if (unlikely(err)) {
		error("struct network constructor failed with error %d.", err);
		goto fail;
	}

	/* Since the master process needs to be part of the network we need
	 * to increment the number of hosts.
	 */
	err = _setup_tree(&self->tree, self->alloc, self->nhosts + 1, here);
	if (unlikely(err))
		goto fail;

	err = optpool_find_by_key_as_int(self->opts, "CommBufpoolSize", &bufpoolsz);
	if (unlikely(err)) {
		fcallerror("optpool_find_by_key_as_int", err);
		bufpoolsz = 128;
	}

	err = buffer_pool_ctor(&self->bufpool,
	                       self->alloc, bufpoolsz);
	if (unlikely(err)) {
		error("struct buffer_pool constructor failed with error %d.", err);
		return err;
	}

	err = optpool_find_by_key_as_int(self->opts, "CommSendqSize", &sendqsz);
	if (unlikely(err)) {
		fcallerror("optpool_find_by_key_as_int", err);
		sendqsz = 128;
	}

	err = optpool_find_by_key_as_int(self->opts, "CommRecvqSize", &recvqsz);
	if (unlikely(err)) {
		fcallerror("optpool_find_by_key_as_int", err);
		recvqsz = 128;
	}

	err = comm_ctor(&self->comm, self->alloc,
	                &self->tree, &self->bufpool,
			sendqsz, recvqsz);
	if (unlikely(err)) {
		error("struct comm constructor failed with error %d.", err);
		return err;
	}

	list_ctor(&self->jobs);

	return 0;

fail:
	_free_hosts(self);

	return err;
}

int spawn_dtor(struct spawn *self)
{
	int err;

	if (unlikely(!list_is_empty(&self->jobs))) {
		error("List of jobs is not empty in spawn_dtor().");
		/* It is probably easiest just to go on and do nothing. If spawn_dtor()
		 * is called the execution will terminate soon anyway and trying to
		 * cleanup is probably a waste of effort.
		 */
	}

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

	err = hostinfo_dtor(&self->hostinfo);
	if (unlikely(err)) {
		error("struct hostinfo destructor failed with error %d.", err);
		return err;
	}

	err = optpool_dtor(self->opts);
	if (unlikely(err)) {
		error("struct optpool destructor failed with error %d.", err);
		return err;
	}

	err = ZFREE(self->alloc, (void **)&self->opts, 1,
	            sizeof(struct optpool), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	if (self->procs) {
		err = ZFREE(self->alloc, (void **)&self->procs, self->nprocs,
		            sizeof(struct process), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	if (self->wkpool) {
		err = exec_worker_pool_dtor(self->wkpool);
		if (unlikely(err)) {
			fcallerror("exec_worker_pool_dtor", err);
			return err;
		}

		err = ZFREE(self->alloc, (void **)&self->wkpool, 1,
		            sizeof(struct exec_worker_pool), "");
		if (err) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	memset(self, 0, sizeof(*self));

	return 0;
}

int spawn_perform_delayed_setup(struct spawn *self,
                                struct optpool *opts)
{
	int err;

	self->opts = opts;

	err = _copy_hosts(self, opts);
	if (unlikely(err))
		return err;

	return 0;
}

int spawn_setup_worker_pool(struct spawn *self, const char *path)
{
	struct plugin *plu;
	int err, tmp;
	int fanout, cap;

	if (unlikely(self->exec)) {
		warn("self->exec is not NULL. This may cause a memory leak.");
		self->exec = NULL;
	}

	plu = load_plugin(path);
	if (unlikely(!plu))
		return -ESOMEFAULT;	/* load_plugin() reported reason. */

	self->exec = cast_to_exec_plugin(plu);
	if (unlikely(!self->exec)) {
		error("Plugin '%s' is not an exec plugin.", path);
		/* TODO We are leaking a plugin.
		 */
		return -EINVAL;
	}

	err = ZALLOC(self->alloc, (void **)&self->wkpool, 1,
	             sizeof(struct exec_worker_pool), "worker pool");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	err = optpool_find_by_key_as_int(self->opts, "ExecFanout", &fanout);
	if (unlikely(err)) {
		fcallerror("optpool_find_by_key_as_int", err);
		die();	/* Should not happen since we have default values
			 * in place. */
	}

	err = optpool_find_by_key_as_int(self->opts, "ExecQueueCapacity", &cap);
	if (unlikely(err)) {
		fcallerror("optpool_find_by_key_as_int", err);
		die();
	}

	err = exec_worker_pool_ctor(self->wkpool, self->alloc,
	                            fanout, cap, self->exec);
	if (unlikely(err)) {
		fcallerror("exec_worker_pool_ctor", err);
		goto fail;
	}

	return 0;

fail:
	tmp = ZFREE(self->alloc, (void **)&self->wkpool, 1,
                     sizeof(struct exec_worker_pool), "");
	if (tmp)
		fcallerror("ZFREE", tmp);

	/* TODO We are leaking a plugin.
	 */

	return err;
}

int spawn_comm_start(struct spawn *self)
{
	int err;

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

int spawn_comm_flush(struct spawn *self)
{
	int err;

	err = comm_flush(&self->comm);
	if (unlikely(err)) {
		fcallerror("comm_flush", err);
		return err;
	}

	return 0;
}

int spawn_comm_resv_channel(struct spawn *self, ui16 *channel)
{
	int err;

	err = comm_resv_channel(&self->comm, channel);
	if (unlikely(err)) {
		fcallerror("comm_resv_channel", err);
		return err;
	}

	return 0;
}

int spawn_send_message(struct spawn *self, struct message_header *header, void *msg)
{
	struct buffer *buffer;
	int err, tmp;

	err = buffer_pool_pull(&self->bufpool, &buffer);
	if (unlikely(err)) {
		error("Failed to obtain buffer.");
		return err;
	}

	err = pack_message(buffer, header, msg);
	if (unlikely(err)) {
		/* FIXME How to handle these kind of errors? We need some
		 *       appropriate reaction strategy and recovery option
		 *       otherwise all this error handling would just be
		 *       a waste of time and we would be better off just
		 *       calling die() anytime we hit a problem.
		 */
		fcallerror("pack_message", err);
		goto fail;
	}

	err = comm_enqueue(&self->comm, buffer);
	if (unlikely(err)) {
		fcallerror("comm_enqueue", err);
		goto fail;
	}

	return 0;

fail:
	assert(err);

	tmp = buffer_pool_push(&self->bufpool, buffer);
	if (unlikely(tmp))
		fcallerror("buffer_pool_push", tmp);

	return err;
}


static int _copy_hosts(struct spawn *self, struct optpool *opts)
{
	int err, tmp;
	const char *hosts;
	char host[64];
	int i, j;
	int nhosts;

	/* TODO Add support for compressed host lists.
	 */

	hosts = optpool_find_by_key(opts, "Hosts");
	if (unlikely(!hosts)) {
		error("Missing 'Hosts' option.");
		die();	/* Impossible anyway. Checked in
			 * _check_important_options() in
			 * main.c
			 */
	}

	nhosts = _count_hosts(hosts);
	if (nhosts < 0) {
		fcallerror("_count_hosts", nhosts);
		return -EINVAL;
	}
	if (nhosts == 0) {
		error("Number of hosts is zero.");
		return -EINVAL;
	}

	/* If we received the number of hosts as part of argv we should
	 * check that the value is actually correct.
	 */
	if ((self->nhosts > 0) && (nhosts != self->nhosts)) {
		error("Number of hosts does not match expected value.");
		die();
	}

	self->nhosts = nhosts;

	err = ZALLOC(self->alloc, (void **)&self->hosts, self->nhosts,
	             sizeof(char *), "host list");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	j = 0;
	for (i = 0; i < self->nhosts; ++i) {
		if (',' == hosts[j])
			++j;
		j += _copy_up_to_char(hosts + j, host, sizeof(host), ',');

		err = xstrdup(self->alloc, host, &self->hosts[i]);
		if (unlikely(err)) {
			fcallerror("xstdrup", err);
			goto fail;
		}
	}

	return 0;

fail:
	tmp = ZFREE(self->alloc, (void **)&self->hosts, self->nhosts,
                    sizeof(char *), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

static int _count_hosts(const char *hosts)
{
	int i, n;
	char host[64];

	n = i = 0;
	while (hosts[i]) {
		if (0 != hosts[i])
			++i;
		i += _copy_up_to_char(hosts + i, host, sizeof(host), ',');

		if (0 == strlen(host)) {
			error("Invalid host in host list at position %d.", n);
			return -EINVAL;
		}

		++n;
	}

	return n;
}

static int _copy_up_to_char(const char *istr, char *ostr, int len, char x)
{
	int i, j;

	i = j = 0;
	while ((0 != istr[i]) && (x != istr[i]) && (j < len - 1))
		ostr[j++] = istr[i++];
	ostr[j] = 0;

	return i;
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

static int _setup_tree(struct network *self, struct alloc *alloc,
                       int size, int here)
{
	int err;

	if (unlikely(size < 0 || here < 0 || here >= size))
		return -EINVAL;

	err = network_resize(self, size);
	if (unlikely(err)) {
		fcallerror("network_resize", err);
		return err;
	}

	self->here = here;

	return 0;
}

static int _tree_close_listenfd(struct network *self)
{
	int err;
	int i;

	for (i = 0; i < self->nlistenfds; ++i) {
		err = do_close(self->listenfds[i]);
		if (unlikely(err))
			return err;	/* do_close() reported reason. */

		self->listenfds[i] = -1;
	}

	return 0;
}

