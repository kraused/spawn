
#ifndef SPAWN_PMI_SERVER_H_INCLUDED
#define SPAWN_PMI_SERVER_H_INCLUDED 1

#include "list.h"


/*
 * Collected state of the PMI server.
 */
struct pmi_server
{
	int 		fd;
	int		rank;
	int		size;

	struct alloc	*alloc;
	struct list	kvs;

	_Bool		initialized;
	_Bool		finalized;
};


int pmi_server_ctor(struct pmi_server *self, struct alloc *alloc,
                    int fd, int rank, int size);
int pmi_server_dtor(struct pmi_server *self);

/*
 * Talk to the client. This function can block if the client is not
 * behaving well.
 *
 * TODO Improve the error handling in the face of misbehaving/malicious clients.
 *
 */
int pmi_server_talk(struct pmi_server *self);

#endif

