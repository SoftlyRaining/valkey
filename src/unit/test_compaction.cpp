/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "generated_wrappers.hpp"

#include <cstdio>
#include <string>

extern "C" {
#include "compaction.h"
#include "sds.h"
#include "zmalloc.h"
}

/* Leak-checking fixture: the queue and all pending keys must be reclaimed. */
class CompactQueueTest : public ::testing::Test {
  protected:
    size_t mem_before = 0;
    void SetUp() override {
        mem_before = zmalloc_used_memory();
    }
    void TearDown() override {
        EXPECT_EQ(zmalloc_used_memory(), mem_before) << "Memory leak detected";
    }
};

TEST_F(CompactQueueTest, EmptyPopReturnsFalse) {
    compactQueue *q = compactQueueCreate();
    EXPECT_EQ(compactQueueLength(q), 0u);
    compactCandidate c;
    EXPECT_FALSE(compactQueuePop(q, &c));
    compactQueueFree(q);
}

TEST_F(CompactQueueTest, FifoOrderAndPayload) {
    compactQueue *q = compactQueueCreate();
    compactQueuePush(q, 3, "alpha", 5);
    compactQueuePush(q, 7, "beta", 4);
    EXPECT_EQ(compactQueueLength(q), 2u);

    compactCandidate c;
    ASSERT_TRUE(compactQueuePop(q, &c));
    EXPECT_EQ(c.dbid, 3);
    EXPECT_EQ(std::string(c.key, sdslen(c.key)), "alpha");
    sdsfree(c.key);

    ASSERT_TRUE(compactQueuePop(q, &c));
    EXPECT_EQ(c.dbid, 7);
    EXPECT_EQ(std::string(c.key, sdslen(c.key)), "beta");
    sdsfree(c.key);

    EXPECT_EQ(compactQueueLength(q), 0u);
    EXPECT_FALSE(compactQueuePop(q, &c));
    compactQueueFree(q);
}

TEST_F(CompactQueueTest, GrowsBeyondInitialCapacityPreservingOrder) {
    compactQueue *q = compactQueueCreate();
    const int N = 100; /* exceeds the initial capacity, forcing growth */
    for (int i = 0; i < N; i++) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "k%08d", i);
        compactQueuePush(q, i, buf, (size_t)n);
    }
    EXPECT_EQ(compactQueueLength(q), (unsigned long)N);
    for (int i = 0; i < N; i++) {
        compactCandidate c;
        ASSERT_TRUE(compactQueuePop(q, &c));
        EXPECT_EQ(c.dbid, i);
        char buf[32];
        snprintf(buf, sizeof(buf), "k%08d", i);
        EXPECT_EQ(std::string(c.key, sdslen(c.key)), buf);
        sdsfree(c.key);
    }
    EXPECT_EQ(compactQueueLength(q), 0u);
    compactQueueFree(q);
}

TEST_F(CompactQueueTest, InterleavedPushPopRingWrap) {
    compactQueue *q = compactQueueCreate();
    /* Keep the ring near-full and wrapping: push 2, pop 1, repeatedly. */
    int next_push = 0, next_pop = 0;
    for (int round = 0; round < 50; round++) {
        compactQueuePush(q, next_push, "x", 1);
        next_push++;
        compactQueuePush(q, next_push, "x", 1);
        next_push++;
        compactCandidate c;
        ASSERT_TRUE(compactQueuePop(q, &c));
        EXPECT_EQ(c.dbid, next_pop);
        next_pop++;
        sdsfree(c.key);
    }
    /* Drain remainder in order. */
    compactCandidate c;
    while (compactQueuePop(q, &c)) {
        EXPECT_EQ(c.dbid, next_pop);
        next_pop++;
        sdsfree(c.key);
    }
    EXPECT_EQ(next_pop, next_push);
    compactQueueFree(q);
}

TEST_F(CompactQueueTest, FreeReclaimsPendingKeys) {
    compactQueue *q = compactQueueCreate();
    for (int i = 0; i < 50; i++) compactQueuePush(q, i, "somekey", 7);
    EXPECT_EQ(compactQueueLength(q), 50u);
    compactQueueFree(q); /* must free the 50 pending keys; TearDown verifies no leak */
}

TEST_F(CompactQueueTest, ShouldEnqueuePolicy) {
    /* Sparse and long enough -> enqueue. */
    EXPECT_TRUE(compactionShouldEnqueue(0.39, 0.50, 1000, 64));
    /* At/above trigger -> skip. */
    EXPECT_FALSE(compactionShouldEnqueue(0.50, 0.50, 1000, 64));
    EXPECT_FALSE(compactionShouldEnqueue(0.80, 0.50, 1000, 64));
    /* Too few items -> skip even when very sparse. */
    EXPECT_FALSE(compactionShouldEnqueue(0.10, 0.50, 10, 64));
    /* min_length boundary is inclusive. */
    EXPECT_TRUE(compactionShouldEnqueue(0.10, 0.50, 64, 64));
}
