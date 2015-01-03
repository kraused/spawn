
#ifndef SPAWN_OPTIONS_H_INCLUDED
#define SPAWN_OPTIONS_H_INCLUDED 1

#include <stdio.h>

#include "list.h"

struct buffer;

/*
 * Options are key value pairs organized in
 * a linked list.
 */
struct kvpair
{
	char		*key;
	char		*val;

	struct list	list;
};

/*
 * A pool of options.
 */
struct optpool
{
	struct alloc	*alloc;
	struct list	opts;
};

/*
 * Create a new (initially empty) optpool instance.
 */
int optpool_ctor(struct optpool *self, struct alloc *alloc);
int optpool_dtor(struct optpool *self);

/*
 * Parse the command line arguments and fill the pool with this
 * information. Previously inserted options will be overwritten
 * if the key matches.
 * The function will stop parsing the command line arguments when
 * it encounters the entry '--', i.e., everything past '--' will
 * not be checked and not inserted in the pool.
 */
int optpool_parse_cmdline_args(struct optpool *self, char **argv);

/*
 * Fill the option pool with options taken from a (configuration) file.
 */
int optpool_parse_file(struct optpool *self, FILE *fp);

/*
 * Find an option by the key. The function returns NULL if no
 * matching option is found.
 */
const char *optpool_find_by_key(struct optpool *self, const char *key);

/*
 * Find an option by the key and return the value (converted to an int)
 * in result. The function returns zero in case of success.
 */
int optpool_find_by_key_as_int(struct optpool *self,
                               const char *key, int *result);

/*
 * Serialize and de-serialize a struct optpool.
 */
int optpool_buffer_pack(struct optpool *self, struct buffer *buffer);
int optpool_buffer_unpack(struct optpool *self, struct buffer *buffer);

#endif

