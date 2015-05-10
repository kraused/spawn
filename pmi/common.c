
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include "compiler.h"
#include "pmi/common.h"


static __thread char _buf[4096];


int pmi_send(int fd, const char *cmd)
{
	int  n;
	char num[7];

	n = strlen(cmd) + 1;	/* +1 for the trailing zero.
				 */

	snprintf(num, sizeof(num), "%6d", n);

	/* FIXME Error handling
	 */

	write(fd, num, 6);	/* Send without trailing zero.
				 * The remaining string is
				 * following directly. */
	write(fd, cmd, n);

//	x = dprintf(fd, "%6lu%s", strlen(cmd), cmd);
//	if (unlikely(x < 0))
//		return 1;

	return PMI_SUCCESS;
}

int pmi_sendf(int fd, const char *fmt, ...)
{
	va_list vl;
	int x;

	va_start(vl, fmt);
	x = vsnprintf(_buf, sizeof(_buf), fmt, vl);
	va_end(vl);

	if (unlikely((x < 0) || (x >= sizeof(_buf))))
		return 1;

//	printf(" (((( %s )))) \n", _buf);
//	fflush(0);

	return pmi_send(fd, _buf);
}

int pmi_recv(int fd, char *buf, ll size)
{
	char num[7];
	int  n;
	ll   x;

	x = pmi_read_bytes(fd, num, 6);

//	printf("fd = %d x = %d\n", fd, x);

	if (unlikely(x < 0))
		return 1;

	num[6] = 0;
	n = strtol(num, NULL, 10);
	if (unlikely(n == 0))
		return 2;

	if (unlikely(n >= size))
		return 3;

	x = pmi_read_bytes(fd, buf, n);
	if (unlikely(x < 0))
		return 4;

	buf[n] = 0;

	return PMI_SUCCESS;
}

int pmi_read_bytes(int fd, void *buf, ll size)
{
	ll x;

	memset(buf, 0, size);

	while (size > 0) {
		/* TODO How to report errors? This code can be
		 *      used by client as well as server code.
		 */
		x = read(fd, buf, size);

		if (unlikely(-1 == x))
			return -errno;

		size -= x;
		buf  += x;
	}

	return 0;
}

int pmi_cmd_parse(struct pmi_unpacked_cmd *cmd, char *buf)
{
	int i, j, k;

	for (i = 0; buf[i]; ++i) {
		if (';' == buf[i]) {
			buf[i] = 0;

			if (strncmp(buf, "cmd=", 4))
				return 1;		/* FIXME! */

			cmd->cmd = &buf[4];
			buf      = &buf[i+1];
			break;
		}
	}

	for (k = 0; k < PMI_MAX_OPTS;) {
		for (i = 0; buf[i]; ++i) {
			if (';' == buf[i]) {
				buf[i] = 0;

				if (0 == i)
					break;

				for (j = 0; buf[j]; ++j) {
					if ('=' == buf[j]) {
						buf[j] = 0;
						break;
					}
				}

				if (j > 0) {
					cmd->keys[k] = &buf[0];
					cmd->vals[k] = &buf[j+1];
					++k;
				}

				buf = &buf[i+1];
				break;
			}
		}

		if (i == 0)
			break;
	}

	cmd->nopts = k;

	return 0;
}

const char *pmi_cmd_opt_find_by_key(struct pmi_unpacked_cmd *cmd, const char *key)
{
	int i;

	for (i = 0; i < cmd->nopts; ++i)
		if (0 == strcmp(cmd->keys[i], key))
			return cmd->vals[i];

	return NULL;
}

int pmi_cmd_opt_find_by_key_as_int(struct pmi_unpacked_cmd *cmd, const char *key, int *value)
{
	const char *x = pmi_cmd_opt_find_by_key(cmd, key);
	if (unlikely(!x))
		return 1;

	*value = strtol(x, NULL, 10);
	return PMI_SUCCESS;
}

