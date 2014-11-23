
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
#include "helper.h"
#include "error.h"


int do_close(int fd)
{
	int err;

	while (1) {
		err = close(fd);
		if (unlikely(err)) {
			if (likely(EINTR == errno))
				continue;

			error("close() failed. errno = %d says '%s'.", 
			      errno, strerror(errno));
			return -1;
		}

		break;
	}
	
	return 0;
}

int do_accept(int sockfd, struct sockaddr *addr, ll *addrlen)
{
	int fd;
	socklen_t len = 0;

	if (unlikely(!addrlen))
		return -EINVAL;

	len = *addrlen;

	while (1) {
		fd = accept(sockfd, addr, &len);
		if (unlikely(fd < 0)) {
			if (likely(EINTR == errno || EAGAIN == errno))
				continue;

			error("accept() failed. errno = %d says '%s'.", 
			      errno, strerror(errno));
			return -1;
		}

		break;
	}

	*addrlen = len;

	return fd;
}

