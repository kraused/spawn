
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "helper.h"
#include "alloc.h"
#include "protocol.h"
#include "pack.h"


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
                                         struct message_response_join *msg);
static int _free_message_response_join(struct alloc *alloc,
                                       const struct message_response_join *msg);
static int _pack_message_ping(struct buffer *buffer,
                              const struct message_ping *msg);
static int _unpack_message_ping(struct buffer *buffer,
                                struct message_ping *msg);
static int _free_message_ping(struct alloc *alloc,
                              const struct message_ping *msg);
static int _pack_message_exec(struct buffer *buffer,
                              const struct message_exec *msg);
static int _unpack_message_exec(struct buffer *buffer,
                                struct alloc *alloc,
                                struct message_exec *msg);
static int _free_message_exec(struct alloc *alloc,
                              const struct message_exec *msg);
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
	case MESSAGE_TYPE_EXEC:
		err = _pack_message_exec(buffer,
		                    (const struct message_exec *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = _pack_message_request_build_tree(buffer,
		                    (const struct message_request_build_tree *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = _pack_message_response_build_tree(buffer,
		                    (const struct message_response_build_tree *)msg);
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
	case MESSAGE_TYPE_EXEC:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_exec),
		             "struct message_exec");
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_request_build_tree),
		             "struct message_request_build_tree");
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_response_build_tree),
		             "struct message_response_build_tree");
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
		err = _unpack_message_response_join(buffer,
		                    (struct message_response_join *)msg);
		break;
	case MESSAGE_TYPE_PING:
		err = _unpack_message_ping(buffer,
		                    (struct message_ping *)msg);
		break;
	case MESSAGE_TYPE_EXEC:
		err = _unpack_message_exec(buffer, alloc,
		                    (struct message_exec *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = _unpack_message_request_build_tree(buffer, alloc,
		                    (struct message_request_build_tree *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = _unpack_message_response_build_tree(buffer,
		                    (struct message_response_build_tree *)msg);
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
	case MESSAGE_TYPE_EXEC:
		err = _free_message_exec(alloc,
		                    (struct message_exec *)msg);
		break;
	case MESSAGE_TYPE_REQUEST_BUILD_TREE:
		err = _free_message_request_build_tree(alloc,
		                    (struct message_request_build_tree *)msg);
		break;
	case MESSAGE_TYPE_RESPONSE_BUILD_TREE:
		err = _free_message_response_build_tree(alloc,
		                    (struct message_response_build_tree *)msg);
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

	return 0;
}

static int _unpack_message_response_join(struct buffer *buffer,
                                         struct message_response_join *msg)
{
	int err;

	err = buffer_unpack_ui32(buffer, &msg->addr, 1);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_response_join(struct alloc *alloc,
                                       const struct message_response_join *msg)
{
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

static int _pack_message_exec(struct buffer *buffer,
                              const struct message_exec *msg)
{
	int err;
	int i;

	err = buffer_pack_string(buffer, msg->host);
	if (unlikely(err))
		return err;

	i = 0;
	while (msg->argv[i]) ++i;

	err = buffer_pack_array_of_str(buffer, i, msg->argv);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_exec(struct buffer *buffer,
                                struct alloc *alloc,
                                struct message_exec *msg)
{
	int err;

	err = buffer_unpack_string(buffer, alloc, (char **)&msg->host);
	if (unlikely(err))
		return err;

	err = buffer_unpack_array_of_str(buffer, alloc,
	                                 &msg->_argc, (char ***)&msg->argv);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_exec(struct alloc *alloc,
                              const struct message_exec *msg)
{
	int err;
	int i;

	for (i = 0; i < msg->_argc; ++i) {
		err = ZFREE(alloc, (void **)&msg->argv[i],
		            strlen(msg->argv[i]) + 1,
		            sizeof(char), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	err = ZFREE(alloc, (void **)&msg->argv, msg->_argc, sizeof(char *), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	err = ZFREE(alloc, (void **)&msg->host, strlen(msg->host) + 1, sizeof(char), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

static int _pack_message_request_build_tree(struct buffer *buffer,
                                            const struct message_request_build_tree *msg)
{
	int err;

	err = buffer_pack_array_of_str(buffer, msg->nhosts, msg->hosts);
	if (unlikely(err))
		return err;

	return 0;
}

static int _unpack_message_request_build_tree(struct buffer *buffer,
                                              struct alloc *alloc,
                                              struct message_request_build_tree *msg)
{
	int err;

	err = buffer_unpack_array_of_str(buffer, alloc, &msg->nhosts, &msg->hosts);
	if (unlikely(err))
		return err;

	return 0;
}

static int _free_message_request_build_tree(struct alloc *alloc,
                                            const struct message_request_build_tree *msg)
{
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

