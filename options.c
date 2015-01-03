
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "options.h"
#include "alloc.h"
#include "helper.h"
#include "pack.h"


static inline int _strcmp2(const char *x, const char *z);
static int _insert_option(struct optpool *self, const char *keqv);
static struct kvpair *_find_option_by_key(struct optpool *self,
                                          const char *key);
static struct kvpair *_find_option_by_key_v2(struct optpool *self,
                                             const char *key, ll len);
static ll _length_list(struct optpool *self);
static int _find_equal_sign(const char *string);


int optpool_ctor(struct optpool *self, struct alloc *alloc)
{
	self->alloc = alloc;

	list_ctor(&self->opts);

	return 0;
}

int optpool_dtor(struct optpool *self)
{
	int err;
	struct list *p;
	struct list *q;
	struct kvpair *opt;

	/* If ZFREE() fails we prematurely quit the function
	 * which will probably result in a memory leak.
	 */

	LIST_FOREACH_S(p, q, &self->opts) {
		opt = LIST_ENTRY(p, struct kvpair, list);

		/* Key and value always share the same storage
		 * area so we only delete the key. Since strfree()
		 * is using the strlen we however need to reconnect
		 * the strings.
		 */
		opt->key[strlen(opt->key)] = '=';
		err = strfree(self->alloc, &opt->key);
		if (unlikely(err))
			return err;

		list_remove(&opt->list);

		err = ZFREE(self->alloc, (void **)&opt, 1,
		            sizeof(struct kvpair), "");
		if (unlikely(err)) {
			fcallerror("ZFREE", err);
			return err;
		}
	}

	return 0;
}

int optpool_parse_cmdline_args(struct optpool *self, char **argv)
{
	int err;
	int i;

	i = 1;
	while (argv[i]) {
		if (_strcmp2("--", argv[i]))
			break;
		if (_strcmp2("-o", argv[i])) {
			err = _insert_option(self, argv[i+1]);
			if (unlikely(err))
				fcallerror("_insert_option", err);
			i += 2;
		} else {
			error("Ignoring unexpected option '%s'", argv[i]);
			++i;
		}
	}

	return 0;
}

int optpool_parse_file(struct optpool *self, FILE *fp)
{
	int err;
	char line[128];
	char x;
	int i, k;

	i = 0;
	do {
		x = fgetc(fp);

		if (('\n' == x) || (EOF == x)) {
			line[i] = 0;
			i = 0;

			k = 0;
			while (line[k] && (' ' == line[k] || '\t' == line[k])) ++k;

			if (0 == line[k])	/* Skip empty lines
						 */
				continue;
			if ('#' == line[k])	/* Skip comments
						 */
				continue;

			err = _insert_option(self, line + k);
			if (unlikely(err))
				fcallerror("_insert_option", err);

			i = 0;
		} else {
			line[i] = x;
			++i;
		}
	} while (x != EOF);

	return 0;
}

const char *optpool_find_by_key(struct optpool *self, const char *key)
{
	struct kvpair *opts;

	opts = _find_option_by_key(self, key);
	if (unlikely(!opts))
		return NULL;

	return opts->val;
}

int optpool_find_by_key_as_int(struct optpool *self,
                               const char *key, int *result)
{
	int err;
	const char *val;
	char *p;

	val = optpool_find_by_key(self, key);
	if (unlikely(!val)) {
		error("Could not find a key-value matching the key '%s'", key);
		return -EINVAL;
	}

	*result = strtol(val, &p, 10);
	if (unlikely(p != (val + strlen(val)))) {
		error("Value '%s' seems be no valid integer in base 10", val);
		return -EINVAL;
	}

	return 0;
}

int optpool_buffer_pack(struct optpool *self, struct buffer *buffer)
{
	int err, i;
	ui64 len;
	struct list *p;
	struct kvpair *opt;

	len = _length_list(self);

	err = buffer_pack_ui64(buffer, &len, 1);
	if (unlikely(err))
		return err;

	LIST_FOREACH(p, &self->opts) {
		opt = LIST_ENTRY(p, struct kvpair, list);

		i = strlen(opt->key);
		opt->key[i] = '=';

		err = buffer_pack_string(buffer, opt->key);
		if (unlikely(err)) {
			opt->key[i] = 0;
			return err;
		}

		opt->key[i] = 0;
	}

	return 0;
}

int optpool_buffer_unpack(struct optpool *self, struct buffer *buffer)
{
	int err;
	int i, j;
	ui64 len;
	struct kvpair *opt;

	err = buffer_unpack_ui64(buffer, &len, 1);
	if (unlikely(err))
		return err;

	for (j = 0; j < (int )len; ++j) {
		err = ZALLOC(self->alloc, (void **)&opt,
		             1, sizeof(struct kvpair), "kvpair");
		if (unlikely(err)) {
			fcallerror("ZALLOC", err);
			return err;
		}

		err = buffer_unpack_string(buffer, self->alloc, &opt->key);
		if (unlikely(err))
			return err;

		i = _find_equal_sign(opt->key);
		if (unlikely(i < 0))
			return i;

		list_insert_before(&self->opts, &opt->list);
	}


	return 0;
}


/*
 * Check that string z equals the string x which has length two.
 * In constrast to strcmp() we return 1 if the strings match.
 */
static inline int _strcmp2(const char *x, const char *z)
{
	return ((z[0] != 0) && (z[0] == x[0]) &&
		(z[1] != 0) && (z[1] == x[1]) &&
		(z[2] == 0));
}

static int _insert_option(struct optpool *self, const char *keqv)
{
	int err;
	struct kvpair *opt;
	int i;

	i = _find_equal_sign(keqv);
	if (unlikely(i < 0))
		return i;

	opt = _find_option_by_key_v2(self, keqv, i);
	if (opt) {
		debug("Overwriting value '%s' by '%s' for key '%s'.",
		      opt->val, keqv + (i + 1), opt->key);

		opt->key[strlen(opt->key)] = '=';
		strfree(self->alloc, &opt->key);	/* Ignore error and
							 * potential leak.
							 */
	} else {
		err = ZALLOC(self->alloc, (void **)&opt, 1,
		             sizeof(struct kvpair), "kvpair");
		if (unlikely(err)) {
			fcallerror("ZALLOC", err);
			return err;
		}

		list_ctor(&opt->list);

		list_insert_before(&self->opts, &opt->list);
	}

	err = xstrdup(self->alloc, keqv, &opt->key);
	if (unlikely(err)) {
		fcallerror("xstrdup", err);
		return err;
	}

	opt->val    = opt->key + (i + 1);
	opt->key[i] = 0;

	return 0;
}

static struct kvpair *_find_option_by_key(struct optpool *self,
                                          const char *key)
{
	struct list *p;
	struct kvpair *opt;

	LIST_FOREACH(p, &self->opts) {
		opt = LIST_ENTRY(p, struct kvpair, list);

		if (!strcmp(key, opt->key))
			return opt;
	}

	return NULL;
}

/*
 * Version of _find_option_by_key() which does not require the key
 * parameter to be NULL terminated.
 */
static struct kvpair *_find_option_by_key_v2(struct optpool *self,
                                             const char *key, ll len)
{
	struct list *p;
	struct kvpair *opt;

	LIST_FOREACH(p, &self->opts) {
		opt = LIST_ENTRY(p, struct kvpair, list);

		if ((len == strlen(opt->key)) &&
                    (!strncmp(key, opt->key, len)))
			return opt;
	}

	return NULL;
}

static ll _length_list(struct optpool *self)
{
	struct list *p;
	ll len;

	len = 0;
	LIST_FOREACH(p, &self->opts) {
		++len;
	}

	return len;
}

static int _find_equal_sign(const char *string)
{
	int i;

	i = 0;
	while (string[i] && ('=' != string[i])) ++i;

	if (unlikely('=' != string[i])) {
		error("Options need to be specified as key-value pairs "
		      "separated by an '=' sign without whitespaces.");
		return -EINVAL;
	}

	return i;
}

