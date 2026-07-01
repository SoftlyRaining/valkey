/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Background load-factor compaction: candidate selection.
 *
 * When a sorted set backed by an ordered index drops below a load-factor
 * trigger after a delete, its (db, key) is enqueued here. A background sweep
 * (driven from serverCron) later drains the queue and runs incremental
 * compaction on each candidate, re-checking the load factor at drain time so
 * stale entries (already compacted, refilled, or deleted) are skipped.
 *
 * The queue is intentionally self-contained (only sds + zmalloc) so the
 * selection logic can be unit-tested without a running server. Per-key
 * de-duplication while a candidate is pending is the caller's responsibility
 * (e.g. a one-bit "queued" flag on the tracked object), keeping the queue a
 * simple FIFO. */

#ifndef COMPACTION_H
#define COMPACTION_H

#include "sds.h"
#include <stdbool.h>
#include <stddef.h>

/* A tree flagged as a candidate for background load-factor compaction. */
typedef struct compactCandidate {
    int dbid;
    sds key; /* owned copy of the key name */
} compactCandidate;

typedef struct compactQueue compactQueue;

/* Lifecycle. compactQueueFree frees the queue and any keys still pending. */
compactQueue *compactQueueCreate(void);
void compactQueueFree(compactQueue *q);

/* Number of pending candidates. */
unsigned long compactQueueLength(const compactQueue *q);

/* Append a candidate, copying `key`. */
void compactQueuePush(compactQueue *q, int dbid, const char *key, size_t keylen);

/* Pop the oldest candidate into *out. The caller takes ownership of out->key
 * and must sdsfree it. Returns false (leaving *out untouched) when empty. */
bool compactQueuePop(compactQueue *q, compactCandidate *out);

/* Trigger policy, centralized so the threshold rule is unit-testable: a tree is
 * worth enqueuing when it holds at least `min_length` items and its load factor
 * has fallen below `trigger`. */
bool compactionShouldEnqueue(double load_factor, double trigger, unsigned long length, unsigned long min_length);

#endif /* COMPACTION_H */
