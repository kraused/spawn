
#ifndef SPAWN_XSTRING_H_INCLUDED
#define SPAWN_XSTRING_H_INCLUDED 1

/*
 * Our version of xstrup which uses the allocator alloc.
 */
int xstrdup(struct alloc *alloc, const char *istr, char **ostr);

/*
 * Duplicate an array of strings.
 */
int array_of_str_dup(struct alloc *alloc, int n, const char **istr,
                     char ***ostr);

/*
 * Free an array of strings.
 */
int array_of_str_free(struct alloc *alloc, int n, char ***str);

#endif

