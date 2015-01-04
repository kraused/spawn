
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "helper.h"
#include "alloc.h"
#include "protocol.h"
#include "pack.h"
#include "options.h"


static int _pack_message_something(struct buffer *buffer, int type, void *msg);
static int _alloc_message_something(struct alloc *alloc, int type, void **msg);
static int _unpack_message_something(struct buffer *buffer, struct alloc *alloc,
                                     int type, void *msg);
static int _free_message_something(struct alloc *alloc, int type, void *msg);
static int _pack_message_request_join(struct buffer *buffer,
                                      const struct message_request_join *msg);
static int _unpack_message_request_join(struct buffer *buffer,
                                        struct message_request_join *msg);
static int _free_message_request_join(struct alloc *alloc,
                                      const struct message_request_join *msg);
static int _pack_message_response_join(struct buffer *buffer,
                                       const struct message_response_join *msg);
static int _unpack_message_response_join(struct buffer *buffer,
                                         struct alloc *alloc,
                                         struct message_response_join *msg);
static int _free_message_response_join(struct alloc *alloc,
                                       const struct message_response_join *msg);
static int _pack_message_ping(struct buffer *buffer,
                              const struct message_ping *msg);
static int _unpack_message_ping(struct buffer *buffer,
                                struct message_ping *msg);
static int _free_message_ping(struct alloc *alloc,
                              const struct message_ping *msg);
static int _pack_message_request_exec(struct buffer *buffer,
                                      const struct message_request_exec *msg);
static int _unpack_message_request_exec(struct buffer *buffer,
                                        struct alloc *alloc,
                                        struct message_request_exec *msg);
static int _free_message_request_exec(struct alloc *alloc,
                                      const struct message_request_exec *msg);
static int _pack_message_request_build_tree(struct buffer *buffer,
                                            const struct message_request_build_tree *msg);
static int _unpack_message_request_build_tree(struct buffer *buffer,
                                              struct alloc *alloc,
                                              struct message_request_build_tree *msg);
static int _free_message_request_build_tree(struct alloc *alloc,
                                            const struct message_request_build_tree *msg);
static int _pack_message_response_build_tree(struct buffer *buffer,
                                             const struct message_response_build_tree *msg);
static int _unpack_message_response_build_tree(struct buffer *buffer,
                                               struct message_response_build_tree *msg);
static int _free_message_response_build_tree(struct alloc *alloc,
                                             const struct message_response_build_tree *msg);
static int _pack_message_request_task(struct buffer *buffer,
                                      const struct message_request_task *msg);
static int _unpack_message_request_task(struct buffer *buffer,
                                        struct alloc *alloc,
                                        struct message_request_task *msg);
static int _free_message_request_task(struct alloc *alloc,
                                      const struct message_request_task *msg);
static int _pack_message_response_task(struct buffer *buffer,
                                      const struct message_response_task *msg);
static int _unpack_message_response_task(struct buffer *buffer,
                                        struct alloc *alloc,
                                        struct message_response_task *msg);
static int _free_message_response_task(struct alloc *alloc,
                                      const struct message_response_task *msg);
static int _pack_message_request_exit(struct buffer *buffer,
                                      const struct message_request_exit *msg);
static int _unpack_message_request_exit(struct buffer *buffer,
                                        struct message_request_exit *msg);
static int _free_message_request_exit(struct alloc *alloc,
                                      const struct message_request_exit *msg);
static int _pack_message_response_exit(struct buffer *buffer,
                                       const struct message_response_exit *msg);
static int _unpack_message_response_exit(struct buffer *buffer,
                                         struct message_response_exit *msg);
static int _free_message_response_exit(struct alloc *alloc,
                                       const struct message_response_exit *msg);


int pack_message_header(struct buffer *buffer,
                        const struct message_header *header)
{
	int err;
	ui16 tmp[6] = {
		header->src,
		header->dst,
		header->flags,
		header->type,
		header->channel,
		header->pad16[0]
	};

	err = buffer_pack_ui16(buffer, tmp, ARRAYLEN(tmp));
	if (unlikely(err))
		return err;
	err = buffer_pack_ui32(buffer, &header->payload, 1);
	if (unlikely(err))
		return err;

	return 0;
}

int unpack_message_header(struct buffer *buffer,
                          struct message_header *header)
{
	int err;
	ui16 tmp[6];

	memset(header, 0, sizeof(*header));

	err = buffer_unpack_ui16(buffer, tmp, ARRAYLEN(tmp));
	if (unlikely(err))
		return err;
	err = buffer_unpack_ui32(buffer, &header->payload, 1);
	if (unlikely(err))
		return err;

	header->src = tmp[0];
	header->dst = tmp[1];
	header->flags = tmp[2];
	header->type = tmp[3];
	header->channel = tmp[4];
	/* ignore pad16 */

	return 0;
}

int pack_message_payload(struct buffer *buffer,
                         const struct message_header *header, void *msg)
{
	int err;

	err = _pack_message_something(buffer, header->type, msg);
	if (unlikely(err))
		return err;

	return 0;
}

int unpack_message_payload(struct buffer *buffer,
                           const struct message_header *header,
                           struct alloc *alloc, void *msg)
{
	int err;

	err = _unpack_message_something(buffer, alloc, header->type, msg);
	if (unlikely(err))
		return err;

	return 0;
}

int free_message_payload(const struct message_header *header,
                         struct alloc *alloc, void *msg)
{
	int err;

	err = _free_message_something(alloc, header->type, msg);
	if (unlikely(err))
		return err;

	return 0;
}

int pack_message(struct buffer *buffer, struct message_header *header, void *msg)
{
	int err;

	err = buffer_seek(buffer, sizeof(*header));
	if (unlikely(err))
		return err;

	header->payload = 0;	/* Trash whatever is stored here */

	err = _pack_message_something(buffer, header->type, msg);
	if (unlikely(err))
		return err;

	header->payload = buffer_size(buffer) - sizeof(*header);

	err = buffer_seek(buffer, 0);
	if (unlikely(err))
		return err;

	err = pack_message_header(buffer, header);
	if (unlikely(err))
		return err;

	return 0;
}

int unpack_message(struct buffer *buffer, struct message_header *header,
                   struct alloc *alloc, void **msg)
{
	int err;

	err = buffer_seek(buffer, 0);
	if (unlikely(err))
		return err;

	err = unpack_message_header(buffer, header);
	if (unlikely(err))
		return err;

	err = _alloc_message_something(alloc, header->type, msg);
	if (unlikely(err))
		return err;

	err = _unpack_message_something(buffer, alloc, header->type, *msg);
	if (unlikely(err))
		return err;

	return 0;
}


static int _pack_message_something(struct buffer *buffer, int type, void *msg)
{
	int err;

	err = -ESOMEFAULT;

	switch (type) {
	case MESSAGE_TYPE_REQUEST_JOIN:
		err = _pack_message_request_join(buffer,
		                    (const struct message_request_join *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_JOIN:
		err = _pack_message_response_join(buffer,
		                    (const struct message_response_join *)msg);
		break;
	case MESSAGE_TYPE_PING:
		err = _pack_message_ping(buffer,
		                    (const struct message_ping *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_EXEC:
		err = _pack_message_request_exec(buffer,
		                    (const struct message_request_exec *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = _pack_message_request_build_tree(buffer,
		                    (const struct message_request_build_tree *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = _pack_message_response_build_tree(buffer,
		                    (const struct message_response_build_tree *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_TASK:
		err = _pack_message_request_task(buffer,
		                    (const struct message_request_task *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_TASK:
		err = _pack_message_response_task(buffer,
		                    (const struct message_response_task *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_EXIT:
		err = _pack_message_request_exit(buffer,
		                    (const struct message_request_exit *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_EXIT:
		err = _pack_message_response_exit(buffer,
		                    (const struct message_response_exit *)msg);
		break;
	default:
		error("Unknown message type %d.", type);
		err = -ESOMEFAULT;
	}

	if (unlikely(err))
		return err;

	return 0;
}

static int _alloc_message_something(struct alloc *alloc, int type, void **msg)
{
	int err;

	err = -ESOMEFAULT;

	switch (type) {
	case MESSAGE_TYPE_REQUEST_JOIN:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_request_join),
		             "struct message_request_join");
		break;
	case MESSAGE_TYPE_RESPONSE_JOIN:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_response_join),
		             "struct message_response_join");
		break;
	case MESSAGE_TYPE_PING:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_ping),
		             "struct message_ping");
		break;
	case MESSAGE_TYPE_REQUEST_EXEC:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_request_exec),
		             "struct message_request_exec");
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_request_build_tree),
		             "struct message_request_build_tree");
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_response_build_tree),
		             "struct message_response_build_tree");
		break;
	case MESSAGE_TYPE_REQUEST_TASK:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_request_task),
		             "struct message_request_task");
		break;
	case MESSAGE_TYPE_RESPONSE_TASK:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_response_task),
		             "struct message_response_task");
		break;
	case MESSAGE_TYPE_REQUEST_EXIT:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_request_exit),
		             "struct message_request_exit");
		break;
	case MESSAGE_TYPE_RESPONSE_EXIT:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_response_exit),
		             "struct message_response_exit");
		break;
	default:
		error("Unknown message type %d.", type);
		err = -ESOMEFAULT;
	}

	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	return 0;
}

static int _unpack_message_something(struct buffer *buffer, struct alloc *alloc,
                                     int type, void *msg)
{
	int err;

	err = -ESOMEFAULT;

	switch (type) {
	case MESSAGE_TYPE_REQUEST_JOIN:
		err = _unpack_message_request_join(buffer,
		                    (struct message_request_join *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_JOIN:
		err = _unpack_message_response_join(buffer, alloc,
		                    (struct message_response_join *)msg);
		break;
	case MESSAGE_TYPE_PING:
		err = _unpack_message_ping(buffer,
		                    (struct message_ping *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_EXEC:
		err = _unpack_message_request_exec(buffer, alloc,
		                    (struct message_request_exec *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = _unpack_message_request_build_tree(buffer, alloc,
		                    (struct message_request_build_tree *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = _unpack_message_response_build_tree(buffer,
		                    (struct message_response_build_tree *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_TASK:
		err = _unpack_message_request_task(buffer, alloc,
		                    (struct message_request_task *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_TASK:
		err = _unpack_message_response_task(buffer, alloc,
		                    (struct message_response_task *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_EXIT:
		err = _unpack_message_request_exit(buffer,
		                    (struct message_request_exit *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_EXIT:
		err = _unpack_message_response_exit(buffer,
		                    (struct message_response_exit *)msg);
		break;
	default:
		error("Unknown message type %d.", type);
		err = -ESOMEFAULT;
	}

	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_something(struct alloc *alloc, int type, void *msg)
{
	int err;

	err = -ESOMEFAULT;

	switch (type) {
	case MESSAGE_TYPE_REQUEST_JOIN:
		err = _free_message_request_join(alloc,
		                    (struct message_request_join *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_JOIN:
		err = _free_message_response_join(alloc,
		                    (struct message_response_join *)msg);
		break;
	case MESSAGE_TYPE_PING:
		err = _free_message_ping(alloc,
		                    (struct message_ping *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_EXEC:
		err = _free_message_request_exec(alloc,
		                    (struct message_request_exec *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = _free_message_request_build_tree(alloc,
		                    (struct message_request_build_tree *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = _free_message_response_build_tree(alloc,
		                    (struct message_response_build_tree *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_TASK:
		err = _free_message_request_task(alloc,
		                    (struct message_request_task *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_TASK:
		err = _free_message_response_task(alloc,
		                    (struct message_response_task *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_EXIT:
		err = _free_message_request_exit(alloc,
		                    (struct message_request_exit *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_EXIT:
		err = _free_message_response_exit(alloc,
		                    (struct message_response_exit *)msg);
		break;
	default:
		error("Unknown message type %d.", type);
		err = -ESOMEFAULT;
	}

	if (unlikely(err))
		return err;

	return 0;
}

static int _pack_message_request_join(struct buffer *buffer,
                                      const struct message_request_join *msg)
{
	int err;

	err = buffer_pack_ui32(buffer, &msg->pid, 1);
	if (unlikely(err))
		return err;
	err = buffer_pack_ui32(buffer, &msg->ip, 1);
	if (unlikely(err))
		return err;
	err = buffer_pack_ui32(buffer, &msg->portnum, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_request_join(struct buffer *buffer,
                                        struct message_request_join *msg)
{
	int err;

	err = buffer_unpack_ui32(buffer, &msg->pid, 1);
	if (unlikely(err))
		return err;
	err = buffer_unpack_ui32(buffer, &msg->ip, 1);
	if (unlikely(err))
		return err;
	err = buffer_unpack_ui32(buffer, &msg->portnum, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_request_join(struct alloc *alloc,
                                      const struct message_request_join *msg)
{
	return 0;
}

static int _pack_message_response_join(struct buffer *buffer,
                                       const struct message_response_join *msg)
{
	int err;

	err = buffer_pack_ui32(buffer, &msg->addr, 1);
	if (unlikely(err))
		return err;

	err = optpool_buffer_pack(msg->opts, buffer);
	if (unlikely(err)) {
		fcallerror("optpool_buffer_pack", err);
		return err;
	}

	return 0;
}

static int _unpack_message_response_join(struct buffer *buffer,
                                         struct alloc *alloc,
                                         struct message_response_join *msg)
{
	int err;

	err = buffer_unpack_ui32(buffer, &msg->addr, 1);
	if (unlikely(err))
		return err;

	err = ZALLOC(alloc, (void **)&msg->opts, 1,
	             sizeof(struct optpool), "opts");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	err = optpool_ctor(msg->opts, alloc);
	if (unlikely(err)) {
		fcallerror("optpool_ctor", err);
		return err;
	}

	err = optpool_buffer_unpack(msg->opts, buffer);
	if (unlikely(err)) {
		fcallerror("optpool_buffer_unpack", err);
		return err;
	}

	return 0;
}

static int _free_message_response_join(struct alloc *alloc,
                                       const struct message_response_join *msg)
{
	/* Since struct optpool can be quite heavy it would be a waste of resources
	 * to duplicate it. Instead the pointer should be copied and set to zero.
	 *
	 * WARNING This means that the correct allocator needs to be passed to
	 *         the unpack routine.
	 */
	if (NULL != msg->opts)
		warn("msg->opts should be NULL. This may result in a memory leak.");

	return 0;
}

static int _pack_message_ping(struct buffer *buffer,
                              const struct message_ping *msg)
{
	int err;

	err = buffer_pack_ui64(buffer, &msg->now, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_ping(struct buffer *buffer,
                                struct message_ping *msg)
{
	int err;

	err = buffer_unpack_ui64(buffer, &msg->now, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_ping(struct alloc *alloc,
                              const struct message_ping *msg)
{
	return 0;
}

static int _pack_message_request_exec(struct buffer *buffer,
                                      const struct message_request_exec *msg)
{
	int err;
	int i;

	err = buffer_pack_string(buffer, msg->host);
	if (unlikely(err))
		return err;

	i = 0;
	while (msg->argv[i]) ++i;
	++i;	/* Trailing NULL pointer */

	err = buffer_pack_array_of_str(buffer, i, msg->argv);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_request_exec(struct buffer *buffer,
                                        struct alloc *alloc,
                                        struct message_request_exec *msg)
{
	int err;

	err = buffer_unpack_string(buffer, alloc, (char **)&msg->host);
	if (unlikely(err))
		return err;

	err = buffer_unpack_array_of_str(buffer, alloc,
	                                 &msg->argc, (char ***)&msg->argv);
	if (unlikely(err))
		return err;

	--msg->argc;	/* argv has always size argc + 1 with argv[argc] == NULL. */

	return 0;
}

static int _free_message_request_exec(struct alloc *alloc,
                                      const struct message_request_exec *msg)
{
	int err;

	err = array_of_str_free(alloc, (msg->argc + 1), (char ***)&msg->argv);
	if (unlikely(err))
		return err;	/* array_of_str_free() reports reason. */

	err = strfree(alloc, (char **)&msg->host);
	if (unlikely(err))
		return err;

	return 0;
}

static int _pack_message_request_build_tree(struct buffer *buffer,
                                            const struct message_request_build_tree *msg)
{
	int err;

	err = buffer_pack_ui64(buffer, &msg->nhosts, 1);
	if (unlikely(err))
		return err;

	err = buffer_pack_si32(buffer, msg->hosts, msg->nhosts);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_request_build_tree(struct buffer *buffer,
                                              struct alloc *alloc,
                                              struct message_request_build_tree *msg)
{
	int err;

	err = buffer_unpack_ui64(buffer, &msg->nhosts, 1);
	if (unlikely(err))
		return err;

	err = ZALLOC(alloc, (void **)&msg->hosts, msg->nhosts, sizeof(si32), "hosts");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	err = buffer_unpack_si32(buffer, msg->hosts, msg->nhosts);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_request_build_tree(struct alloc *alloc,
                                            const struct message_request_build_tree *msg)
{
	int err;

	err = ZFREE(alloc, (void **)&msg->hosts, msg->nhosts, sizeof(si32), "hosts");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

static int _pack_message_response_build_tree(struct buffer *buffer,
                                             const struct message_response_build_tree *msg)
{
	int err;

	err = buffer_pack_ui32(buffer, &msg->deads, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_response_build_tree(struct buffer *buffer,
                                               struct message_response_build_tree *msg)
{
	int err;

	err = buffer_unpack_ui32(buffer, &msg->deads, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_response_build_tree(struct alloc *alloc,
                                             const struct message_response_build_tree *msg)
{
	return 0;
}

static int _pack_message_request_task(struct buffer *buffer,
                                      const struct message_request_task *msg)
{
	int err;

	err = buffer_pack_string(buffer, msg->path);
	if (unlikely(err))
		return err;

	err = buffer_pack_ui32(buffer, &msg->channel, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_request_task(struct buffer *buffer,
                                        struct alloc *alloc,
                                        struct message_request_task *msg)
{
	int err;

	err = buffer_unpack_string(buffer, alloc, (char **)&msg->path);
	if (unlikely(err))
		return err;

	err = buffer_unpack_ui32(buffer, &msg->channel, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_request_task(struct alloc *alloc,
                                      const struct message_request_task *msg)
{
	int err;

	err = strfree(alloc, (char **)&msg->path);
	if (unlikely(err))
		return err;	/* strfree() will report problem. */

	return 0;
}

static int _pack_message_response_task(struct buffer *buffer,
                                       const struct message_response_task *msg)
{
	int err;

	err = buffer_pack_ui32(buffer, &msg->ret, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_response_task(struct buffer *buffer,
                                         struct alloc *alloc,
                                         struct message_response_task *msg)
{
	int err;

	err = buffer_unpack_ui32(buffer, &msg->ret, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_response_task(struct alloc *alloc,
                                       const struct message_response_task *msg)
{
	return 0;
}

static int _pack_message_request_exit(struct buffer *buffer,
                                      const struct message_request_exit *msg)
{
	int err;

	err = buffer_pack_ui32(buffer, &msg->signum, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_request_exit(struct buffer *buffer,
                                        struct message_request_exit *msg)
{
	int err;

	err = buffer_unpack_ui32(buffer, &msg->signum, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_request_exit(struct alloc *alloc,
                                      const struct message_request_exit *msg)
{
	return 0;
}

static int _pack_message_response_exit(struct buffer *buffer,
                                       const struct message_response_exit *msg)
{
	int err;

	err = buffer_pack_ui32(buffer, &msg->dummy, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_response_exit(struct buffer *buffer,
                                         struct message_response_exit *msg)
{
	int err;

	err = buffer_unpack_ui32(buffer, &msg->dummy, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_response_exit(struct alloc *alloc,
                                       const struct message_response_exit *msg)
{
	return 0;
}

