
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "hostinfo.h"
#include "helper.h"


static int _discover_ipv4interfaces(struct alloc *alloc, int *nifs,
                                    struct _ipv4interface **ifs);
static int _do_ifconf_ioctl(int sock, struct alloc *alloc,
                            struct ifconf* ifc);


int hostinfo_ctor(struct hostinfo *self, struct alloc *alloc)
{
	int err;

	memset(self, 0, sizeof(struct hostinfo));

	self->alloc = alloc;

	err = gethostname(self->hostname, sizeof(self->hostname));
	if (unlikely(err < 0)) {
		error("gethostname failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}

	err = _discover_ipv4interfaces(alloc, &self->nipv4ifs, &self->ipv4ifs);
	if (unlikely(err)) {
		fcallerror("_discover_ipv4interfaces", err);
		return err;
	}

	return 0;
}

int hostinfo_dtor(struct hostinfo *self)
{
	int err;

	err = ZFREE(self->alloc, (void **)&self->ipv4ifs, self->nipv4ifs,
	            sizeof(struct _ipv4interface), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;
}

static int _discover_ipv4interfaces(struct alloc *alloc, int *nifs,
                                    struct _ipv4interface **ifs)
{
	int err, tmp;
	int sock;
	struct ifconf ifc;
	struct ifreq  ifr;
	int n, i, k;
	char buf1[64];
	char buf2[64];

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (unlikely(sock < 0)) {
		error("socket() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}

	err = _do_ifconf_ioctl(sock, alloc, &ifc);
	if (unlikely(err)) {
		fcallerror("_do_ifconf_ioctl", err);
		return err;
	}

	n = 0;
	for (i = 0; i < (int )ifc.ifc_len/sizeof(struct ifreq); ++i)
		n += (AF_INET == ifc.ifc_req[i].ifr_addr.sa_family);

	*nifs = n;

	err = ZALLOC(alloc, (void **)ifs, (*nifs),
	             sizeof(struct _ipv4interface), "ifs");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	debug("Found %d interfaces.", (*nifs));

	i = 0;
	for (k = 0; k < (int )ifc.ifc_len/sizeof(struct ifreq); ++k)
	{
		if (AF_INET != ifc.ifc_req[k].ifr_addr.sa_family)
			break;
		
		ifr = ifc.ifc_req[k];

		memcpy((*ifs)[i].name, ifc.ifc_req[k].ifr_name, sizeof((*ifs)[i].name));

		err = ioctl(sock, SIOCGIFADDR, &ifr);
		if (unlikely(err < 0)) {
			error("ioctl() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			err = -errno;
			goto fail;
		}
		
		(*ifs)[i].addr = *(struct sockaddr_in *)&ifr.ifr_addr;

		err = ioctl(sock, SIOCGIFNETMASK, &ifr);
		if (unlikely(err < 0)) {
			error("ioctl() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			err = -errno;
			goto fail;
		}
		
		(*ifs)[i].netmask = *(struct sockaddr_in *)&ifr.ifr_addr;

		/* Copy to a buffer since we cannot do printf("", inet_ntoa(), inet_ntoa())
		 * since inet_ntoa() is not pure.
		 */
		strcpy(buf1, inet_ntoa((*ifs)[i].addr.sin_addr));
		strcpy(buf2, inet_ntoa((*ifs)[i].netmask.sin_addr));
		debug("Interface %d = {.name = %s, .addr = %s, .netmask = %s}", i,
		      (*ifs)[i].name, buf1, buf2);

		++i;
	}

	err = ZFREE(alloc, (void **)&ifc.ifc_buf, ifc.ifc_len, sizeof(char), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		goto fail;
	}

	return 0;

fail:
	tmp = ZFREE(alloc, (void **)&ifc.ifc_buf, ifc.ifc_len, sizeof(char), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	tmp = ZFREE(alloc, (void **)ifs, (*nifs),
	            sizeof(struct _ipv4interface), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	*nifs = 0;

	return err;
}

static int _do_ifconf_ioctl(int sock, struct alloc *alloc,
                            struct ifconf* ifc)
{
	int err;
	ll last, len;
	int done;
	char *buf;

	done = 0;
	last = 0;
	len  = 8*sizeof(struct ifreq);

	/* TODO This loop results in at least one ZALLOC()/ZFREE() too much
	 *      than necessary.
	 */
	while (!done) {
		err = ZALLOC(alloc, (void **)&buf, len, sizeof(char), "");
		if (unlikely(err)) {
			fcallerror("ZALLOC", err);
			return err;
		}

		ifc->ifc_len = len;
		ifc->ifc_buf = buf;

		err = ioctl(sock, SIOCGIFCONF, ifc);
		if (unlikely(err < 0)) {
			error("ioctl() failed. errno = %d says '%s'.",
			      errno, strerror(errno));
			err  = -errno;
			done = 1;	/* Take the ZFREE() to cleanup */
		} else {
			err  = 0;

			/* We doubled the buffer but did not get any additional
			 * information so we are probably done - Of course we allocated
			 * twice as much memory as we actually need but well ...
			 */
			if (last == ifc->ifc_len)
				break;

			last = ifc->ifc_len;
		}

		err = ZFREE(alloc, (void **)&buf, len, sizeof(char), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}

		len = (len << 1);
	}

	if (unlikely(err))
		return err;

	return 0;
}

