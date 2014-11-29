
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

	/* addr and addrlen can be null but not both at the same time. */
	if ((!addr && addrlen) || (addr && !addrlen))
		return -EINVAL;

	if (addrlen)
		len = *addrlen;

	while (1) {
		fd = accept(sockfd, addr, &len);
		if (unlikely(fd < 0)) {
			if (likely(EINTR == errno || EAGAIN == errno))
				continue;

			error("accept() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			return -errno;
		}

		break;
	}

	if (addrlen)
		*addrlen = len;

	return fd;
}

int xstrdup(struct alloc *alloc, const char *istr, char **ostr)
{
	int err;
	int n;

	if (unlikely(!istr || !ostr))
		return -EINVAL;

	n = strlen(istr) + 1;

	err = ZALLOC(alloc, (void **)ostr, n, sizeof(char), "xstrdup");
	if (unlikely(err)) {
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	memcpy(*ostr, istr, n);

	return 0;
}

int array_of_str_dup(struct alloc *alloc, int n, const char **istr,
                     char ***ostr)
{
	int err, tmp;
	int i;

	if (unlikely(!istr || !ostr))
		return -EINVAL;

	err = ZALLOC(alloc, (void **)ostr, n, sizeof(char *), "xstrduplist");
	if (unlikely(err)) {
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	for (i = 0; i < n; ++i) {
		err = xstrdup(alloc, istr[i], &(*ostr)[i]);
		if (unlikely(err))
			goto fail;
	}

	return 0;

fail:
	for (i = 0; i < n; ++i) {
		if (!(*ostr)[i])
			break;

		/* Do not overwrite err at this point! */
		tmp = FREE(alloc, (void **)&(*ostr)[i], strlen((*ostr)[i]) + 1,
		           sizeof(char), "");
		if (unlikely(tmp))
			error("FREE() failed with error %d.", tmp);
	}

	return err;
}

int array_of_str_free(struct alloc *alloc, int n, char ***str)
{
	/* FIXME Not implemented. */
	return -ENOTIMPL;
}

