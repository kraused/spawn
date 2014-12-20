
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <sys/time.h>

#include "config.h"
#include "compiler.h"
#include "helper.h"
#include "error.h"
#include "alloc.h"


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
			return -errno;
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

int do_connect(int sockfd, struct sockaddr *addr, ll addrlen)
{
	int fd;

	while (1) {
		fd = connect(sockfd, addr, addrlen);
		if (unlikely(fd < 0)) {
			if (likely(EINTR == errno || EAGAIN == errno))
				continue;

			error("connect() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			return -errno;
		}

		break;
	}

	return fd;
}

int do_poll(struct pollfd *fds, int nfds, int timeout, int *num)
{
	int err;

	while (1) {
		err = poll(fds, nfds, timeout);
		if (unlikely(err < 0)) {
			if (likely(EINTR == errno))
				continue;

			error("poll() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			return -errno;
		}

		break;
	}

	/* If err is >= 0 this equals the number of structures in which
	 * the revents member is nonzero. */
	*num = err;

	return 0;

}

int do_write(int fd, void *buf, ll size, ll *bytes)
{
	ll x;

	while (1) {
		x = write(fd, buf, size);
		if (unlikely(-1 == x)) {
			if (likely(EINTR == errno))
				continue;

			error("write() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			return -errno;
		}

		*bytes = x;

		break;
	}

	return 0;
}

int do_read(int fd, void *buf, ll size, ll *bytes)
{
	ll x;

	while (1) {
		x = read(fd, buf, size);
		if (unlikely(-1 == x)) {
			if (likely(EINTR == errno))
				continue;

			error("read() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			return -errno;
		}

		*bytes = x;

		break;
	}

	return 0;
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
		fcallerror("ZALLOC", err);
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
		fcallerror("ZALLOC", err);
		return err;
	}

	for (i = 0; i < n; ++i) {
		err = xstrdup(alloc, istr[i], &(*ostr)[i]);
		if (unlikely(err))
			goto fail;
	}

	return 0;

fail:
	assert(err);

	for (i = 0; i < n; ++i) {
		if (!(*ostr)[i])
			break;

		/* Do not overwrite err at this point! */
		tmp = FREE(alloc, (void **)&(*ostr)[i], strlen((*ostr)[i]) + 1,
		           sizeof(char), "");
		if (unlikely(tmp))
			fcallerror("FREE", tmp);
	}

	return err;
}

int array_of_str_free(struct alloc *alloc, int n, char ***str)
{
	/* FIXME Not implemented. */
	return -ENOTIMPL;
}

int daemonize()
{
	ll p;

	umask(0);	/* umask() always succeeds. */

	p = fork();
	if (unlikely(-1 == p)) {
		error("fork() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}
	if (p) {
		exit(0);
	}

	p = setsid();
	if (-1 == p) {
		error("setsid() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;

	}

	p = fork();
	if (unlikely(-1 == p)) {
		error("fork() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}
	if (p) {
		exit(0);
	}

	/* Do not chdir("/"). */

	return 0;
}

ll gettid()
{
	return (long )syscall(SYS_gettid);
}

ll llnow()
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return tv.tv_sec;
}

