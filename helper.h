
#ifndef SPAWN_HELPER_H_INCLUDED
#define SPAWN_HELPER_H_INCLUDED 1

#include "ints.h"


#define MAX(X,Y)	(((X) >= (Y)) ? (X) : (Y))
#define MIN(X,Y)	(((X) >= (Y)) ? (Y) : (X))


int do_close(int fd);
int do_accept(int sockfd, struct sockaddr *addr, ll *addrlen);

#endif

