
#ifndef SPAWN_PMI_COMMON_H_INCLUDED
#define SPAWN_PMI_COMMON_H_INCLUDED 1

#include "ints.h"

/*
 * common.h: Shared private(!) header for client and server
 *           implementation.
 */

#define PMI_VERSION	2
#define PMI_SUBVERSION	0

#define PMI_INIT_STRING "cmd=init pmi_version=2 pmi_subversion=0\n"

#undef  PMI_SUCESS
#define PMI_SUCCESS	0

/*
 * Maximal number of options (key/value pairs) in one command.
 */
#define PMI_MAX_OPTS	16

/*
 * Maximal length of the key and value strings in the KVS.
 */
#define PMI_KVPAIR_MAX_STRLEN	32

/*
 * Decoded/unpacked PMI command.
 */
struct pmi_unpacked_cmd
{
	const char	*cmd;
	int		nopts;
	const char	*keys[PMI_MAX_OPTS];
	const char	*vals[PMI_MAX_OPTS];
};


int pmi_send (int fd, const char *cmd);
int pmi_sendf(int fd, const char *fmt, ...);

int pmi_recv(int fd, char *buf, ll size);

/*
 * Parse the buffer. The buffer is modified to separate the individual substrings
 * with trailing zeros. Since strings are not copied, the lifetime of buf should be
 * at least as long as the lifetime of the cmd instance.
 */
int pmi_cmd_parse(struct pmi_unpacked_cmd *cmd, char *buf);
const char *pmi_cmd_opt_find_by_key(struct pmi_unpacked_cmd *cmd, const char *key);
int pmi_cmd_opt_find_by_key_as_int(struct pmi_unpacked_cmd *cmd, const char *key, int *value);

int pmi_read_bytes(int fd, void *buf, ll size);

#endif

