
#ifndef SPAWN_PROTOCOL_H_INCLUDED
#define SPAWN_PROTOCOL_H_INCLUDED 1

#include "ints.h"


struct alloc;
struct buffer;


/*
 * TODO In order to support heterogeneous systems we need to have
 *      clearly defined datatypes (signed, unsigned, width) and
 *      endianess.
 */

/*
 * Message types
 */
enum
{
	REQUEST_JOIN	= 1001,
	RESPONSE_JOIN
};

/*
 * Message flags
 */
enum
{
	MESSAGE_FLAG_UCAST = 0,	/* default */
	MESSAGE_FLAG_BCAST = 1
};

/*
 * Message header for all protocol messages. Make sure this
 */
struct __attribute__((packed)) message_header
{
	/* The choice of 16 bit integers at this point limits the
	 * network size to 2^16 participants.
	 * dst is meaningless for broadcast messages.
	 */
	ui16	src;
	ui16	dst;

	/* Set MESSAGE_FLAG_BCAST to create a broadcast package. */
	ui16	flags;
	ui16	type;		/* Message size */
	/* In order to separate traffic for individual plugins we support a
	 * number of (virtual) channels.
	 */
	ui16	channel;

	ui16	pad16[1];	/* I prefer manual padding over the packing by the
				 * the compiler (the attribute is just for safety). */

	ui32	payload;	/* Payload size */
};

struct message_request_join
{
	ui32	pid;	/* Process id. 32-bit should be enough on any
			 * platform I am aware of. */
};

struct message_response_join
{
	ui32	addr;	/* Already given to process on command line. Given again for
			 * double checking. */
};

/*
 * Pack a message header into the buffer.
 */
int pack_message_header(struct buffer *buffer,
                        const struct message_header *header);

/*
 * Unpack a message header from the buffer.
 */
int unpack_message_header(struct buffer *buffer,
                          struct message_header *header);

/*
 * Pack the message payload into the buffer. The message header is used
 * to specify the message type but is left unchanged otherwise.
 */
int pack_message_payload(struct buffer *buffer,
                         const struct message_header *header, void *msg);

/*
 * Unpack the message payload from the buffer into msg. In contrast to the
 * function unpack_message() the message buffer itself is assumed to be already
 * allocated.
 */
int unpack_message_payload(struct buffer *buffer,
                           const struct message_header *header,
                           struct alloc *alloc, void *msg);

/*
 * Pack a message.
 *
 * The payload entry in header is ignored on entry and contains the actual
 * payload size in the buffer on exit.
 */
int pack_message(struct buffer *buffer,
                 struct message_header *header, void *msg);

/*
 * Unpack a message.
 *
 * The msg pointer is set to a heap structure the type of which
 * depends on the message type as stored in the header.
 */
int unpack_message(struct buffer *buffer, struct message_header *header,
                   struct alloc *alloc, void **msg);

#endif

