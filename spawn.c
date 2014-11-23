
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
#include "spawn.h"
#include "helper.h"
#include "error.h"


int spawn_start_listen(struct spawn *self, struct sockaddr *addr,
                       unsigned long long addrlen, int backlog)
{
	int fd;
	int err;

	if (unlikely(-1 != self->listenfd)) {
		error("File descriptor is valid on entry of spawn_start_listen().");
		return -1;
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

	err = listen(fd, backlog);
	if (unlikely(err)) {
		error("listen() failed. errno = %d says '%s'.", errno, strerror(errno));
		close(fd);
		return -errno;
	}

	self->listenfd = fd;
	return 0;
}

int spawn_close_listen(struct spawn *self)
{
	int err;

	if (unlikely(-1 == self->listenfd))
		return -1;

	err = do_close(self->listenfd);
	if (unlikely(err))
		return -1;	/* do_close() reported reason. */

	self->listenfd = -1;
	return 0;
}

int spawn_ctor(struct spawn *self, struct alloc *alloc)
{
	memset(self, 0, sizeof(*self));

	self->alloc = alloc;
	self->listenfd = -1;

	return 0;
}

int spawn_dtor(struct spawn *self)
{
	memset(self, 0, sizeof(*self));

	return 0;
}

