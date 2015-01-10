
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "hostinfo.h"
#include "helper.h"


struct _interim_route
{
	struct sockaddr_in	dest;
	struct sockaddr_in	genmask;
	int			ifindex;
};

static int _query_ipv4interfaces(struct alloc *alloc, int *nifs,
                                 struct _ipv4interface **ifs);
static int _do_ifconf_ioctl(int sock, struct alloc *alloc,
                            struct ifconf* ifc);
static int _hostinfo_query_routes(struct hostinfo *self);
static int _send_getroute_request(int sock);
static int _recv_getroute_response(int sock, struct alloc *alloc,
                                   int *nroutes, struct _interim_route **routes);
static int _fill_interim_route(struct rtmsg *msg, int len, struct _interim_route *route);
static int _maybe_realloc_routes(struct alloc *alloc, int i, int *nroutes,
                                 struct _interim_route **routes);
static int _hostinfo_copy_routes(struct hostinfo *self,
                                 struct _interim_route *xroutes);
static int _count_newlines(const char *file, int *count);


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

	err = _query_ipv4interfaces(alloc, &self->nipv4ifs, &self->ipv4ifs);
	if (unlikely(err)) {
		fcallerror("_query_ipv4interfaces", err);
		return err;
	}

	err = _hostinfo_query_routes(self);
	if (unlikely(err)) {
		fcallerror("_query_routes", err);
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

static int _query_ipv4interfaces(struct alloc *alloc, int *nifs,
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

		(*ifs)[i].index = ifc.ifc_req[k].ifr_ifindex;

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

	err = do_close(sock);
	if (unlikely(err))
		return err;

	return 0;

fail:
	do_close(sock);

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

static int _hostinfo_query_routes(struct hostinfo *self)
{
	int err;
	int sock;
	struct _interim_route *xroutes;

	/* We could parse /proc/net/route but let us see how to do this with
	 * netlink sockets.
	 */

	sock = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (unlikely(sock < 0)) {
		error("socket() failed. errno = %d says '%s'.",
		      errno, strerror(errno));
		return -errno;
	}

	err = _send_getroute_request(sock);
	if (unlikely(err)) {
		fcallerror("_send_getroute_msg", err);
		goto fail;
	}

	err = _recv_getroute_response(sock, self->alloc, &self->nroutes, &xroutes);
	if (unlikely(err)) {
		fcallerror("_recv_getroute_response", err);
		goto fail;
	}

	err = _hostinfo_copy_routes(self, xroutes);
	if (unlikely(err)) {
		fcallerror("_resolve_and_copy_routes", err);
		goto fail;
	}

	err = do_close(sock);
	if (unlikely(err))
		return err;

	return 0;

fail:
	do_close(sock);

	return err;
}

static int _send_getroute_request(int sock)
{
	int err;
	struct {
		struct nlmsghdr	header;
		struct rtmsg	payload;
	} msg;

	memset(&msg, 0, sizeof(msg));

	msg.header.nlmsg_len    = NLMSG_LENGTH(sizeof(struct rtmsg));	/* == sizeof(msg) */
	msg.header.nlmsg_type   = RTM_GETROUTE;
	msg.header.nlmsg_flags  = NLM_F_REQUEST | NLM_F_DUMP;

	msg.payload.rtm_family  = AF_INET;
	msg.payload.rtm_dst_len = 0;
	msg.payload.rtm_src_len = 0;
	msg.payload.rtm_table   = RT_TABLE_MAIN;

	err = do_write_loop(sock, &msg, sizeof(msg));
	if (unlikely(err))
		return err;

	return 0;
}

static int _recv_getroute_response(int sock, struct alloc *alloc,
                                   int *nroutes, struct _interim_route **routes)
{
	int err, tmp;
	char *buf;
	const ll size = 1048576;	/* 1 MiB should be large enough
					 */
	ll len;
	struct nlmsghdr *header;
	int done;
	struct rtmsg *msg;
	int i;

	err = _count_newlines("/proc/net/route", nroutes);
	if(unlikely(err) || (0 == *nroutes)) {
		fcallerror("_count_newlines", err);
		*nroutes = 1;
	} else {
		--(*nroutes);
	}

	err = ZALLOC(alloc, (void **)routes, (*nroutes),
	             sizeof(struct _interim_route), "");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	err = ZALLOC(alloc, (void **)&buf, size, sizeof(char), "");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	/* The first version of this code used separate reads for header and payload
	 * so that we could dynamically adapt the buffer size but this deadlocked because
	 * apparently one has to read a whole route entry in one read()/recv() call.
	 */

	done = 0;
	i = 0;
	while (!done) {
		err = do_read(sock, buf, size, &len);
		if (unlikely(err))
			goto fail;

		header = (struct nlmsghdr *)buf;

		while (NLMSG_OK(header, len)) {
			if (NLMSG_DONE == header->nlmsg_type) {
				done = 1;
				break;
			}

			if (header->nlmsg_type == NLMSG_ERROR) {
				error("header->nlmsg_type == NLMSG_ERROR");
				err = -ESOMEFAULT;	/* Could read the following
							 * struct nlmsgerr.
							 */
				goto fail;
			}

			msg = (struct rtmsg *)NLMSG_DATA(header);
			if (RT_TABLE_MAIN != msg->rtm_table)
				break;

			err = _maybe_realloc_routes(alloc, i, nroutes, routes);
			if (unlikely(err))
				goto fail;

			err = _fill_interim_route(msg, RTM_PAYLOAD(header), &(*routes)[i]);
			if (unlikely(err)) {
				fcallerror("_fill_interim_route", err);
				goto fail;
			}

			++i;

			header = NLMSG_NEXT(header, len);
		}
	}

	if (unlikely(i != (*nroutes))) {
		error("Estimated number of routing table "
		      "entries was too high (%d vs %d).", i, (*nroutes));
		die();
	}

	err = ZFREE(alloc, (void **)&buf, size, sizeof(char), "");
	if (unlikely(err)) {
		fcallerror("ZFREE", err);
		return err;
	}

	return 0;

fail:
	tmp = ZFREE(alloc, (void **)&buf, size, sizeof(char), "");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return err;
}

static int _maybe_realloc_routes(struct alloc *alloc, int i, int *nroutes,
                                 struct _interim_route **routes)
{
	int err;

	/* Happens only if counting lines in /proc/net/route did
	 * not work properly.
	 */
	if (i == (*nroutes)) {
		err = ZREALLOC(alloc, (void **)routes,
		               (*nroutes), sizeof(struct _interim_route),
		               (*nroutes) + 1, sizeof(struct _interim_route), "");
		if (unlikely(err)) {
			fcallerror("ZREALLOC", err);
			return err;
		}

		(*nroutes) += 1;
	}

	return 0;
}

static int _fill_interim_route(struct rtmsg *msg, int len, struct _interim_route *route)
{
	struct rtattr *attr;
	unsigned long dest = 0;
	int ifindex = -1;

	attr = (struct rtattr *)RTM_RTA(msg);

	while (RTA_OK(attr, len)) {
		switch (attr->rta_type) {
		case RTA_DST:
			dest  = *(unsigned long *)RTA_DATA(attr);
			break;
		case RTA_OIF:
			ifindex = *(int *)RTA_DATA(attr);
			break;
		}

		attr = RTA_NEXT(attr, len);
	}

	route->genmask.sin_addr.s_addr = htonl((~((int )0)) << (32 - msg->rtm_dst_len));
	route->dest.sin_addr.s_addr = dest;
	route->ifindex = ifindex;

	return 0;
}

static int _hostinfo_copy_routes(struct hostinfo *self,
                                 struct _interim_route *xroutes)
{
	int err, tmp;
	int i, j;
	char buf1[64], buf2[64];

	err = ZALLOC(self->alloc, (void **)&self->routes,
	             self->nroutes, sizeof(struct _route), "routes");
	if (unlikely(err)) {
		fcallerror("ZALLOC", err);
		return err;
	}

	debug("Found %d routing table entries.", self->nroutes);

	for (i = 0; i < self->nroutes; ++i) {
		self->routes[i].dest    = xroutes[i].dest;
		self->routes[i].genmask = xroutes[i].genmask;

		self->routes[i].iface = NULL;
		for (j = 0; j < self->nipv4ifs; ++j)
			if (self->ipv4ifs[j].index == xroutes[i].ifindex) {
				self->routes[i].iface = &self->ipv4ifs[j];
			}

		if (unlikely(!self->routes[i].iface)) {
			error("Could not find interface with index %d.", xroutes[i].ifindex);
			goto fail;
		}

		strcpy(buf1, inet_ntoa(self->routes[i].dest.sin_addr));
		strcpy(buf2, inet_ntoa(self->routes[i].genmask.sin_addr));

		debug("Routing to %s/%s via %s.", buf1, buf2, self->routes[i].iface->name);
	}

	return 0;

fail:
	tmp = ZFREE(self->alloc, (void **)&self->routes,
                     self->nroutes, sizeof(struct _route), "routes");
	if (unlikely(tmp))
		fcallerror("ZFREE", tmp);

	return tmp;
}

static int _count_newlines(const char *file, int *count)
{
	FILE *fp;
	int c;

	fp = fopen(file, "r");
	if (unlikely(!fp)) {
		error("Could not open file '%s' for reading.", file);
		return -ESOMEFAULT;
	}

	*count = 0;
	do {
		c = fgetc(fp);
		if ('\n' == c) ++(*count);
	} while(EOF != c);

	fclose(fp);

	return 0;
}

