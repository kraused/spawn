
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "helper.h"	/* For MIN() and MAX() macros. Make sure not to create dependencies
			 * on functions as we do not link against the library.
			 */
#include "pmi/client.h"
#include "pmi/common.h"


static int _pmi_fd();

static __thread char _buf[4096];


int PMI2_Init(int threaded, int *spawned, int *size, int *rank, int *appnum, int *version, int *subversion)
{
	int x;
	int rc;
	struct pmi_unpacked_cmd cmd;

	if (threaded)
		return 1;

	/* FIXME Error handling. do_write()?
	 */
	write(_pmi_fd(), PMI_INIT_STRING, strlen(PMI_INIT_STRING) + 1);

	x = pmi_sendf(_pmi_fd(), "cmd=fullinit;threaded=false;");
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_recv(_pmi_fd(), _buf, sizeof(_buf));
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_parse(&cmd, _buf);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_opt_find_by_key_as_int(&cmd, "rc", &rc);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	if (rc) {
		/* TODO Use the errrmsg to improve error reporting.
		 */
		return rc;
	}

	x = pmi_cmd_opt_find_by_key_as_int(&cmd, "rank", rank);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_opt_find_by_key_as_int(&cmd, "size", size);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	*version    = PMI_VERSION;
	*subversion = PMI_SUBVERSION;

	return PMI_SUCCESS;
}

int PMI2_Finalize()
{
	int x;
	int rc;
	struct pmi_unpacked_cmd cmd;

	x = pmi_sendf(_pmi_fd(), "cmd=finalize;thrid=0;");
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_recv(_pmi_fd(), _buf, sizeof(_buf));
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_parse(&cmd, _buf);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_opt_find_by_key_as_int(&cmd, "rc", &rc);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	if (rc) {
		/* TODO Use the errrmsg to improve error reporting.
		 */
		return rc;
	}

	return PMI_SUCCESS;
}

int PMI2_KVS_Put(const char *key, const char *value)
{
	int x;
	int rc;
	struct pmi_unpacked_cmd cmd;

	x = pmi_sendf(_pmi_fd(), "cmd=kvs-put;thrid=0;key=%s;value=%s;", key, value);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_recv(_pmi_fd(), _buf, sizeof(_buf));
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_parse(&cmd, _buf);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_opt_find_by_key_as_int(&cmd, "rc", &rc);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	if (rc) {
		/* TODO Use the errrmsg to improve error reporting.
		 */
		return rc;
	}

	return PMI_SUCCESS;
}

int PMI2_KVS_Get(const char *job_id, int src_pmi_id, const char *key, char *value, int maxval, int *vallen)
{
	int x;
	const char *kvsval;
	int rc;
	struct pmi_unpacked_cmd cmd;

	if (unlikely(!key) || (0 == value)) {
		*vallen = 0;
		return 1;
	}

	memset(value, 0, maxval);	/* To simplify debugging */
	*vallen  = 0;

	x = pmi_sendf(_pmi_fd(), "cmd=kvs-get;thrid=0;key=%s;", key);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_recv(_pmi_fd(), _buf, sizeof(_buf));
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_parse(&cmd, _buf);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_opt_find_by_key_as_int(&cmd, "rc", &rc);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	if (rc) {
		/* TODO Use the errrmsg to improve error reporting.
		 */
		return rc;
	}

	kvsval = pmi_cmd_opt_find_by_key(&cmd, "value");
	if (unlikely(!kvsval))
		return 1;

	x = snprintf(value, maxval, "%s", kvsval);
	*vallen = MIN(x, maxval);

	return PMI_SUCCESS;
}

int PMI2_KVS_Fence()
{
	int x;
	int rc;
	struct pmi_unpacked_cmd cmd;

	x = pmi_sendf(_pmi_fd(), "cmd=kvs-fence;thrid=0;");
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_recv(_pmi_fd(), _buf, sizeof(_buf));
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_parse(&cmd, _buf);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	x = pmi_cmd_opt_find_by_key_as_int(&cmd, "rc", &rc);
	if (unlikely(PMI_SUCCESS != x))
		return x;

	if (rc) {
		/* TODO Use the errrmsg to improve error reporting.
		 */
		return rc;
	}

	return PMI_SUCCESS;
}


static int _pmi_fd()
{
	static int fd = -1;
	const char* str;

	if (likely(-1 !=fd))
		return fd;

	str = getenv("PMI_FD");

	if (str) {
		fd = (int )strtol(str, NULL, 10);

		if (0 == fd)
			fd = -1;
	}

	return fd;
}

