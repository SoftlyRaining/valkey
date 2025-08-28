/*
 * Shared declarations for fbtree benchmarks.
 */

#ifndef FBTREE_BENCH_DECLS_H
#define FBTREE_BENCH_DECLS_H

extern "C" {
#include "fbtree_ordered_index.h"

/* Forward declare sds functions (types come from fbtree_ordered_index.h) */
sds sdsnewlen(const void *init, size_t initlen);
sds sdsdup(const_sds s);
void sdsfree(sds s);
}

#endif /* FBTREE_BENCH_DECLS_H */
