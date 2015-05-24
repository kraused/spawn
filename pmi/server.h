
#ifndef SPAWN_PMI_SERVER_H_INCLUDED
#define SPAWN_PMI_SERVER_H_INCLUDED 1

#include "list.h"


struct pmi_server;

/*
 * Implementation of the fence operation. This cannot be implemented here
 * in the PMI2 library but must be done by the resource management itself.
 */
typedef int (*pmi_kvs_fence_impl)(struct pmi_server *self, void *ctx);

/*
 * Collected state of the PMI server.
 */
struct pmi_server
{
	int 			fd;
	int			rank;
	int			size;

	struct alloc		*alloc;
	struct list		kvs;

	_Bool			initialized;
	_Bool			finalized;

	pmi_kvs_fence_impl	kvs_fence;
	void			*kvs_fence_ctx;
};


int pmi_server_ctor(struct pmi_server *self, struct alloc *alloc,
                    int fd, int rank, int size,
                    pmi_kvs_fence_impl kvs_fence,
                    void *kvs_fence_ctx);
int pmi_server_dtor(struct pmi_server *self);

/*
 * Talk to the client. This function can block if the client is not
 * behaving well.
 *
 * TODO Improve the error handling in the face of misbehaving/malicious clients.
 *
 */
int pmi_server_talk(struct pmi_server *self);

int pmi_server_kvs_pack  (struct pmi_server *self, struct alloc *alloc, ui8 **bytes, ui64 *len);
int pmi_server_kvs_unpack(struct pmi_server *self, const ui8 *bytes, ui64 len);
int pmi_server_kvs_free  (struct pmi_server *self);

#endif

