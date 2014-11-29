
#include <string.h>

#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "xstring.h"


int xstrdup(struct alloc *alloc, const char *istr, char **ostr)
{
	int err;
	int n;

	if (unlikely(!istr || !ostr))
		return -EINVAL;

	n = strlen(istr) + 1;

	err = ZALLOC(alloc, (void **)ostr, n, sizeof(char), "xstrdup");
	if (unlikely(err)) {
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	memcpy(*ostr, istr, n);

	return 0;
}

int array_of_str_dup(struct alloc *alloc, int n, const char **istr,
                     char ***ostr)
{
	int err, tmp;
	int i;

	if (unlikely(!istr || !ostr))
		return -EINVAL;

	err = ZALLOC(alloc, (void **)ostr, n, sizeof(char *), "xstrduplist");
	if (unlikely(err)) {
		error("ZALLOC() failed with error %d.", err);
		return err;
	}

	for (i = 0; i < n; ++i) {
		err = xstrdup(alloc, istr[i], &(*ostr)[i]);
		if (unlikely(err))
			goto fail;
	}

	return 0;

fail:
	for (i = 0; i < n; ++i) {
		if (!(*ostr)[i])
			break;

		/* Do not overwrite err at this point! */
		tmp = FREE(alloc, (void **)&(*ostr)[i], strlen((*ostr)[i]) + 1, 
		           sizeof(char), "");
		if (unlikely(tmp))
			error("FREE() failed with error %d.", tmp);
	}

	return err;
}

int array_of_str_free(struct alloc *alloc, int n, char ***str)
{
	/* FIXME Not implemented. */
	return -ENOTIMPL;
}

