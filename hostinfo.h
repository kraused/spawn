
#ifndef SPAWN_HOSTINFO_H_INCLUDED
#define SPAWN_HOSTINFO_H_INCLUDED 1

#include <limits.h>	/* For HOST_NAME_MAX */
#include <netinet/ip.h>
#include <net/if.h>

struct _ipv4interface;

/*
 * Information about the execution host.
 */
struct hostinfo
{
	struct alloc		*alloc;

	char			hostname[HOST_NAME_MAX];

	/* Network interfaces. We only care about AF_INET
	 * and ignore AF_INET6 for now.
	 */
	int			nipv4ifs;
	struct _ipv4interface	*ipv4ifs;

	/* FIXME Routing table.
	 */	
};

/*
 * A network interface (IPv4).
 */
struct _ipv4interface
{
	char			name[IFNAMSIZ];
	struct sockaddr_in	addr;
	struct sockaddr_in	netmask;
};


int hostinfo_ctor(struct hostinfo *self, struct alloc *alloc);
int hostinfo_dtor(struct hostinfo *self);

#endif

