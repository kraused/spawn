
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
	MESSAGE_TYPE_REQUEST_JOIN	= 1001,
	MESSAGE_TYPE_RESPONSE_JOIN,
	MESSAGE_TYPE_PING,
	MESSAGE_TYPE_EXEC,
	MESSAGE_TYPE_REQUEST_BUILD_TREE,
	MESSAGE_TYPE_RESPONSE_BUILD_TREE
};

/*
 * Message flags
 */
enum
{
	MESSAGE_FLAG_UCAST = 0x1,
	MESSAGE_FLAG_BCAST = 0x2
};

/*
 * Message header for all protocol messages. The payload size may not be null!
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

	ui32	payload;	/* Payload size. May not be zero. */
};

struct message_request_join
{
	ui32	pid;		/* Process id. 32-bit should be enough on any
				 * platform I am aware of. */
	ui32	ip;		/* IPv4 address */
	ui32	portnum;	/* TCP port */
};

struct message_response_join
{
	ui32	addr;	/* Already given to process on command line. Given again for
			 * double checking. */
};

struct message_ping
{
	ui64	now;
};

struct message_exec
{
	const char	*host;
	ui64		argc;
	char 		**argv;
};

struct message_request_build_tree
{
	ui64	nhosts;
	char	**hosts;
};

struct message_response_build_tree
{
	ui32	deads;
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
 * Free a message payload
 */
int free_message_payload(const struct message_header *header,
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

