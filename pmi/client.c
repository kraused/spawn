
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "pmi/client.h"

static int _pmi_fd();


int PMI2_Init(int *spawned, int *size, int *rank, int *appnum, int *version, int *subversion)
{
	int fd;
	fd = _pmi_fd();	

	dprintf(fd, "cmd=init pmi_version=2 pmi_subversion=?\n");

	return 0;
}

int PMI2_Finalize()
{
	return 0;
}

int PMI2_KVS_Put(const char *key, const char *value)
{
	return 0;
}

int PMI2_KVS_Get(const char *job_id, int src_pmi_id, const char *key, char *value, int maxval, int *vallen)
{
	return 0;
}

int PMI2_KVS_Fence()
{
	int fd = _pmi_fd();
	char *buf = "cmd=kvs-fence;thrid=1;";

	dprintf(fd, "%6lu%s", strlen(buf) + 1, buf);

	return 0;
}


static int _pmi_fd()
{
	static int fd = -1;
	const char* str;
	char *tailptr;

	if (likely(-1 !=fd))
		return fd;

	str = getenv("PMI_FD");

	if (str) {
		fd = (int )strtol(str, &tailptr, 0);

		if (unlikely(0 != *tailptr))
			fd = -1;
	}

	return fd;
}

