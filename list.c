
#include "config.h"
#include "compiler.h"
#include "error.h"
#include "alloc.h"
#include "list.h"


ll list_length(struct list *self)
{
	struct list *p;
	ll n = 0;

	LIST_FOREACH(p, self) {
		++n;
	}

	return n;
}

