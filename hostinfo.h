
#ifndef SPAWN_HOSTINFO_H_INCLUDED
#define SPAWN_HOSTINFO_H_INCLUDED 1

#include <limits.h>	/* For HOST_NAME_MAX */
#include <netinet/ip.h>
#include <net/if.h>

struct _ipv4interface;
struct _route;

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
	struct ipv4interface	*ipv4ifs;

	/* Routing table.
	 */
	int			nroutes;
	struct route		*routes;
	struct route		*defroute;
};

/*
 * A network interface (IPv4).
 */
struct ipv4interface
{
	int			index;
	char			name[IFNAMSIZ];
	struct sockaddr_in	addr;
	struct sockaddr_in	netmask;
};

struct route
{
	struct sockaddr_in	dest;
	struct sockaddr_in	genmask;
	struct ipv4interface	*iface;
};


int hostinfo_ctor(struct hostinfo *self, struct alloc *alloc);
int hostinfo_dtor(struct hostinfo *self);

/*
 * For a given hostname find the interface through which the node is reachable.
 */
int map_hostname_to_interface(struct hostinfo *hi, const char *host,
                              struct ipv4interface **iface);

/*
 * For a given IPv4 address find the interface through which the node is reachable.
 */
int map_address_to_interface(struct hostinfo *hi,
                             const struct sockaddr_in *addr,
                             struct ipv4interface **iface);

#endif

