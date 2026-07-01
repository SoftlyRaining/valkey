/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "compaction.h"
#include "zmalloc.h"

#include <string.h>

#define COMPACT_QUEUE_INITIAL_CAP 16

/* FIFO ring buffer of candidates. Grows by doubling; never shrinks (the queue
 * is drained steadily, so capacity tracks peak backlog). */
struct compactQueue {
    compactCandidate *slots;
    size_t cap;
    size_t head;  /* index of oldest entry */
    size_t count; /* number of pending entries */
};

compactQueue *compactQueueCreate(void) {
    compactQueue *q = zmalloc(sizeof(*q));
    q->slots = zmalloc(sizeof(compactCandidate) * COMPACT_QUEUE_INITIAL_CAP);
    q->cap = COMPACT_QUEUE_INITIAL_CAP;
    q->head = 0;
    q->count = 0;
    return q;
}

void compactQueueFree(compactQueue *q) {
    if (!q) return;
    for (size_t i = 0; i < q->count; i++) {
        sdsfree(q->slots[(q->head + i) % q->cap].key);
    }
    zfree(q->slots);
    zfree(q);
}

unsigned long compactQueueLength(const compactQueue *q) {
    return (unsigned long)q->count;
}

/* Grow to `newcap` and re-linearize so head == 0. */
static void compactQueueGrow(compactQueue *q, size_t newcap) {
    compactCandidate *slots = zmalloc(sizeof(compactCandidate) * newcap);
    for (size_t i = 0; i < q->count; i++) {
        slots[i] = q->slots[(q->head + i) % q->cap];
    }
    zfree(q->slots);
    q->slots = slots;
    q->cap = newcap;
    q->head = 0;
}

void compactQueuePush(compactQueue *q, int dbid, const char *key, size_t keylen) {
    if (q->count == q->cap) compactQueueGrow(q, q->cap * 2);
    size_t tail = (q->head + q->count) % q->cap;
    q->slots[tail].dbid = dbid;
    q->slots[tail].key = sdsnewlen(key, keylen);
    q->count++;
}

bool compactQueuePop(compactQueue *q, compactCandidate *out) {
    if (q->count == 0) return false;
    *out = q->slots[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return true;
}

bool compactionShouldEnqueue(double load_factor, double trigger, unsigned long length, unsigned long min_length) {
    return length >= min_length && load_factor < trigger;
}
