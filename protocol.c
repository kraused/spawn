
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
static int _unpack_message_something(struct buffer *buffer, int type, void *msg);
static int _pack_message_request_join(struct buffer *buffer,
                                      const struct message_request_join *msg);
static int _unpack_message_request_join(struct buffer *buffer,
                                        struct message_request_join *msg);
static int _pack_message_response_join(struct buffer *buffer,
                                       const struct message_response_join *msg);
static int _unpack_message_response_join(struct buffer *buffer,
                                         struct message_response_join *msg);


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

	err = _unpack_message_something(buffer, header->type, msg);
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

	err = _unpack_message_something(buffer, header->type, *msg);
	if (unlikely(err))
		return err;

	return 0;
}


static int _pack_message_something(struct buffer *buffer, int type, void *msg)
{
	int err;

	err = -ESOMEFAULT;

	switch (type) {
	case REQUEST_JOIN:
		err = _pack_message_request_join(buffer,
		                    (const struct message_request_join *)msg);
		break;
	case RESPONSE_JOIN:
		err = _pack_message_response_join(buffer,
		                    (const struct message_response_join *)msg);
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
	case REQUEST_JOIN:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_request_join),
		             "struct message_request_join");
		break;
	case RESPONSE_JOIN:
		err = ZALLOC(alloc, msg, 1, sizeof(struct message_response_join),
		             "struct message_response_join");
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

static int _unpack_message_something(struct buffer *buffer, int type, void *msg)
{
	int err;

	err = -ESOMEFAULT;

	switch (type) {
	case REQUEST_JOIN:
		err = _unpack_message_request_join(buffer,
		                    (struct message_request_join *)msg);
		break;
	case RESPONSE_JOIN:
		err = _unpack_message_response_join(buffer,
		                    (struct message_response_join *)msg);
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

