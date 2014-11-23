
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "config.h"
#include "compiler.h"
#include "alloc.h"


static int _libc_malloc(struct alloc *self, void **p, ll num, ll sz,
                        const char* file, const char* func, long line, const char* fmt, ...);
static int _libc_zalloc(struct alloc *self, void **p, ll num, ll sz,
                        const char* file, const char* func, long line, const char* fmt, ...);
static int _libc_realloc(struct alloc *self, void **p, ll onum, ll osz, ll nnum, ll nsz,
                         const char* file, const char* func, long line, const char* fmt, ...);
static int _libc_zrealloc(struct alloc *self, void **p, ll onum, ll osz, ll nnum, ll nsz,
                          const char* file, const char* func, long line, const char* fmt, ...);
static int _libc_free(struct alloc *self, void **p, ll num, ll sz,
                      const char* file, const char* func, long line, const char* fmt, ...);
static int _libc_zfree(struct alloc *self, void **p, ll num, ll sz,
                       const char* file, const char* func, long line, const char* fmt, ...);

struct libc_alloc
{
	struct alloc	base;
	int		debug;
};

static struct alloc_ops _libc_alloc_ops = {
	.malloc = _libc_malloc,
	.zalloc = _libc_zalloc,
	.realloc = _libc_realloc,
	.zrealloc = _libc_zrealloc,
	.free = _libc_free,
	.zfree = _libc_zfree
};

static struct libc_alloc _libc_alloc_no_debug = {
	.base  = {
		.ops = &_libc_alloc_ops
	},
	.debug = 0
};

static struct libc_alloc _libc_alloc_wi_debug = {
	.base  = {
		.ops = &_libc_alloc_ops
	},
	.debug = 1
};


struct alloc *libc_allocator()
{
	return (struct alloc *)&_libc_alloc_no_debug;
}

struct alloc *libc_allocator_with_debugging()
{
	return (struct alloc *)&_libc_alloc_wi_debug;
}


static int _libc_malloc(struct alloc *self, void **p, ll num, ll sz,
                        const char* file, const char* func, long line, const char* fmt, ...)
{
	if (unlikely(!self || !p || (num < 0) || (sz < 0)))
		return -EINVAL;

	*p = malloc(num*sz);

	if (unlikely(((struct libc_alloc *)self)->debug)) {	/* optimize for non-debug path */
		/* TODO */
	}

	if (unlikely(!(*p)))
		return -ENOMEM;

	return 0;
}

static int _libc_zalloc(struct alloc *self, void **p, ll num, ll sz,
                        const char* file, const char* func, long line, const char* fmt, ...)
{
	if (unlikely(!self || !p || (num < 0) || (sz < 0)))
		return -EINVAL;

	*p = calloc(num, sz);

	if (unlikely(((struct libc_alloc *)self)->debug)) {	/* optimize for non-debug path */
		/* TODO */
	}

	if (unlikely(!(*p)))
		return -ENOMEM;

	return 0;
}

static int _libc_realloc(struct alloc *self, void **p, ll onum, ll osz, ll nnum, ll nsz,
                         const char* file, const char* func, long line, const char* fmt, ...)
{
	if (unlikely(!self || !p || (onum < 0) || (osz < 0) || (nnum < 0) || (nsz < 0)))
		return -EINVAL;

	*p = realloc(*p, nnum*nsz);

	if (unlikely(((struct libc_alloc *)self)->debug)) {	/* optimize for non-debug path */
		/* TODO */
	}

	if (unlikely(!(*p)))
		return -ENOMEM;

	return 0;
}

static int _libc_zrealloc(struct alloc *self, void **p, ll onum, ll osz, ll nnum, ll nsz,
                         const char* file, const char* func, long line, const char* fmt, ...)
{
	if (unlikely(!self || !p || (onum < 0) || (osz < 0) || (nnum < 0) || (nsz < 0)))
		return -EINVAL;

	*p = realloc(*p, nnum*nsz);

	if (unlikely(((struct libc_alloc *)self)->debug)) {	/* optimize for non-debug path */
		/* TODO */
	}

	if (unlikely(!(*p)))
		return -ENOMEM;

	if ((nnum*nsz) > (onum*osz))
		memset((*p) + (onum*osz), 0, (nnum*nsz) - (onum*osz));

	return 0;
}

static int _libc_free(struct alloc *self, void **p, ll num, ll sz,
                      const char* file, const char* func, long line, const char* fmt, ...)
{
	if (unlikely(!self || !p || (num < 0) || (sz < 0)))
		return -EINVAL;

	if (unlikely(((struct libc_alloc *)self)->debug)) {	/* optimize for non-debug path */
		/* TODO */
	}

	free(*p);
	*p = NULL;

	return 0;
}

static int _libc_zfree(struct alloc *self, void **p, ll num, ll sz,
                       const char* file, const char* func, long line, const char* fmt, ...)
{
	if (unlikely(!self || !p || (num < 0) || (sz < 0)))
		return -EINVAL;

	if (unlikely(((struct libc_alloc *)self)->debug)) {	/* optimize for non-debug path */
		/* TODO */
	}

	free(memset(*p, 0, num*sz));
	*p = NULL;

	return 0;
}

