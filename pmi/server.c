
#include <string.h>
#include <stdio.h>

#include "compiler.h"
#include "error.h"
#include "alloc.h"

#include "pmi/server.h"
#include "pmi/common.h"


/*
 * An entry in the KVS.
 */
struct pmi_kvpair
{
	char		key[PMI_KVPAIR_MAX_STRLEN];
	char		val[PMI_KVPAIR_MAX_STRLEN];

	struct list	list;
};


static int _handle_fullinit(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd);
static int _handle_finalize(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd);
static int _handle_kvs_put(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd);
static int _handle_kvs_get(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd);
static int _handle_kvs_fence(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd);
static int _handle_abort(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd);

static __thread char _buf[4096];
static struct _pmi_supported_cmd {
	const char *cmd;
	int (*handler)(struct pmi_server *, struct pmi_unpacked_cmd *);
} _pmi_server_cmds[] = {
	{"fullinit", _handle_fullinit},
	{"finalize", _handle_finalize},
	{"kvs-put", _handle_kvs_put},
	{"kvs-get", _handle_kvs_get},
	{"kvs-fence", _handle_kvs_fence},
	{"abort", _handle_abort},
	{NULL, NULL}
};


int pmi_server_ctor(struct pmi_server *self, struct alloc *alloc,
                    int fd, int rank, int size)
{
	memset(self, 0, sizeof(*self));

	self->fd    = fd;
	self->rank  = rank;
	self->size  = size;
	self->alloc = alloc;

	list_ctor(&self->kvs);

	return 0;
}

int pmi_server_dtor(struct pmi_server *self)
{
	int err;
	struct list *p;
	struct list *q;
	struct pmi_kvpair *kv;

	/* If ZFREE() fails we prematurely quit the function
	 * which will probably result in a memory leak.
	 */

	LIST_FOREACH_S(p, q, &self->kvs) {
		kv = LIST_ENTRY(p, struct pmi_kvpair, list);

		err = ZFREE(self->alloc, (void **)&kv, 1,
		            sizeof(struct pmi_kvpair), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	return 0;
}

int pmi_server_talk(struct pmi_server *self)
{
	int x;
	int err;
	const char *errmsg;
	struct pmi_unpacked_cmd cmd;
	struct _pmi_supported_cmd *z;

	static int first = 1;

	memset(_buf, 0, sizeof(_buf));	/* To simplify debugging. */

	/* TODO We need to be very careful with returning from this function.
	 *      since the client will usually block until a response is received.
	 *      On the other hand, also the server may be blocked if the client
	 *      is not reading the response.
	 */

	if (unlikely(first)) {
		first = 0;

		x = pmi_read_bytes(self->fd, _buf, strlen(PMI_INIT_STRING) + 1);
		if (unlikely(x)) {
			cmd.cmd = "init";
			err     = 1;
			errmsg  = "Failed to read init string";
			goto fail;
		}

		if (unlikely(strcmp(_buf, PMI_INIT_STRING))) {
			cmd.cmd = "init";
			err     = 2;
			errmsg  = "Init string does not match. Cannot handle this situation";
			goto fail;
		}
	}

	x = pmi_recv(self->fd, _buf, sizeof(_buf));
	if (unlikely(PMI_SUCCESS != x)) {
		cmd.cmd = "?";	/* TODO What should we do in this situation? */
		err     = 3;
		errmsg  = "Failed to read message from client.";
		goto fail;
	}

	x = pmi_cmd_parse(&cmd, _buf);
	if (unlikely(PMI_SUCCESS != x)) {
		cmd.cmd = "?";	/* TODO What should we do in this situation? */
		err     = 4;
		errmsg  = "Failed to parse command.";
		goto fail;
	}

	if (unlikely(self->finalized)) {
		err    = 5;
		errmsg = "Init call after finalize.";
		goto fail;
	}

	if (unlikely((!self->initialized) && strcmp("fullinit", cmd.cmd))) {
		err    = 6;
		errmsg = "PMI call before init.";
		goto fail;
	}

	z = _pmi_server_cmds;
	while (z->cmd) {
		if (0 == strcmp(z->cmd, cmd.cmd)) {
			x = z->handler(self, &cmd);
			break;
		}

		++z;
	}

	if (unlikely(NULL == z->cmd)) {
		err    = 7;
		errmsg = "Unsupported command.";
		goto fail;
	}

	if (unlikely(x)) {
		err    = x;
		errmsg = "Failure in processing the command.";
		goto fail;
	}

	if (0 == strcmp(cmd.cmd, "fullinit"))
		self->initialized = 1;
	if (0 == strcmp(cmd.cmd, "finalize"))
		self->finalized   = 1;

	return 0;

fail:
	pmi_sendf(self->fd, "cmd=%s;thrid=0;rc=%d;errmsg=%s;", cmd.cmd, err, errmsg);
	return -err;
}


static int _handle_fullinit(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd)
{
	return pmi_sendf(srv->fd, "cmd=fullinit-response;rank=%d;size=%d;rc=0;", srv->rank, srv->size);
}

static int _handle_finalize(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd)
{
	return pmi_sendf(srv->fd, "cmd=finalize-response;thrid=0;rc=0;");
}

static int _handle_kvs_put(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd)
{
	int k;
	int err;
	struct pmi_kvpair *kv;
	const char *key = pmi_cmd_opt_find_by_key(cmd, "key");
	const char *val = pmi_cmd_opt_find_by_key(cmd, "value");

	if (unlikely((!key) || (!val))) {
		log("Received a kvs-put command without key or without value.");

		err = pmi_sendf(srv->fd, "cmd=kvs-put-response;thrid=0;rc=1;errmsg=Key or value missing.;");
		return 1;
	}

	err = ZALLOC(srv->alloc, (void **)&kv,
	             1, sizeof(struct pmi_kvpair), "kv");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);

		err = pmi_sendf(srv->fd, "cmd=kvs-put-response;thrid=0;rc=1;errmsg=Memory allocation failed.;");
		return 2;
	}

	k = snprintf(kv->key, PMI_KVPAIR_MAX_STRLEN, "%s", key);
	if (unlikely(k >= PMI_KVPAIR_MAX_STRLEN))
		log("Warning: KVS string truncated.");

	k = snprintf(kv->val, PMI_KVPAIR_MAX_STRLEN, "%s", val);
	if (unlikely(k >= PMI_KVPAIR_MAX_STRLEN))
		log("Warning: KVS string truncated.");

	list_ctor(&kv->list);
	list_insert_before(&srv->kvs, &kv->list);

	return pmi_sendf(srv->fd, "cmd=kvs-put-response;thrid=0;rc=0;");
}

static int _handle_kvs_get(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd)
{
	int err;
	const char *val = NULL;
	struct list *p;
	struct pmi_kvpair *kv;
	const char *key = pmi_cmd_opt_find_by_key(cmd, "key");

	if (unlikely(!key)) {
		err = pmi_sendf(srv->fd, "cmd=kvs-get-response;thrid=0;flag=false;rc=1;errmsg=Key missing.;");
		return 1;
	}

	LIST_FOREACH(p, &srv->kvs) {
		kv = LIST_ENTRY(p, struct pmi_kvpair, list);

		if (0 == strcmp(kv->key, key)) {
			val = kv->val;
			break;
		}
	}

	if (unlikely(!val)) {
		err = pmi_sendf(srv->fd, "cmd=kvs-get-response;thrid=0;flag=false;rc=1;errmsg=Key not found.;");
		return 1;
	}

	return pmi_sendf(srv->fd, "cmd=kvs-get-response;thrid=0;flag=true;value=%s;rc=0;", val);
}

static int _handle_kvs_fence(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd)
{
	return PMI_SUCCESS;
}

static int _handle_abort(struct pmi_server *srv, struct pmi_unpacked_cmd *cmd)
{
	return PMI_SUCCESS;
}

