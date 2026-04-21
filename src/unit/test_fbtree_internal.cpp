/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Internal fbtree tests — uses internal struct access to construct specific
 * tree shapes for testing merge logic and other implementation details.
 * Production code should NOT include fbtree_ordered_index_internal.h.
 */

#include "generated_wrappers.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

extern "C" {
#include "fbtree_ordered_index.h"
#include "fbtree_ordered_index_internal.h"
#include "sds.h"
#include "zmalloc.h"
}

/* ========== Spec types for declarative tree construction ========== */

struct LeafSpec {
    int num_items;
};

struct InnerSpec;
using ChildSpec = std::variant<LeafSpec, InnerSpec>;

/* Inner node spec. depth = levels below this node (1 = leaf children,
 * 2 = inner children with leaves, etc.). total_children = target child count.
 * Explicit children placed first, remaining slots auto-filled with filler. */
struct InnerSpec {
    int depth;
    int total_children;
    std::vector<ChildSpec> explicit_children;
};

/* Convenience constructors */
static LeafSpec leaf(int num = MIN_FILL) {
    return LeafSpec{num};
}

static InnerSpec n(int depth, int total, std::initializer_list<ChildSpec> children = {}) {
    return InnerSpec{depth, total, children};
}

/* ========== Key generation ========== */

static sds makeKey(const std::string &prefix, int item_idx) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", item_idx);
    std::string key = prefix + buf + '\0';
    return sdsnewlen(key.data(), key.size());
}

/* ========== Tree builder ========== */

static node *buildSubtree(const ChildSpec &spec, const std::string &prefix,
                           leafNode **leaf_tail);

static node *buildLeaf(const LeafSpec &spec, const std::string &prefix, leafNode **leaf_tail) {
    leafNode *lf = (leafNode *)zmalloc(sizeof(leafNode));
    memset(lf, 0, sizeof(leafNode));
    lf->header.is_leaf = true;
    lf->header.num_items = spec.num_items;
    for (int i = 0; i < spec.num_items; i++) {
        lf->values[i] = makeKey(prefix, i);
    }
    if (*leaf_tail) {
        (*leaf_tail)->next = lf;
        lf->prev = *leaf_tail;
    }
    *leaf_tail = lf;
    return (node *)lf;
}

/* Finalize an innerNode after children are built: compute child_sizes,
 * child_num_items, anchors, prefix, and features. */
static void finalizeInner(innerNode *in) {
    for (int i = 0; i < in->header.num_items; i++) {
        if (in->children[i]->is_leaf) {
            in->child_sizes[i] = in->children[i]->num_items;
            in->anchors[i] = leafNodeHighKey((leafNode *)in->children[i]);
        } else {
            innerNode *ci = (innerNode *)in->children[i];
            size_t total = 0;
            for (int j = 0; j < ci->header.num_items; j++) total += ci->child_sizes[j];
            in->child_sizes[i] = total;
            in->anchors[i] = ci->anchors[ci->header.num_items - 1];
        }
        in->child_num_items[i] = in->children[i]->num_items;
    }
    if (in->header.num_items > 0) {
        sds first = in->anchors[0];
        sds last = in->anchors[in->header.num_items - 1];
        size_t min_len = sdslen(first) < sdslen(last) ? sdslen(first) : sdslen(last);
        size_t plen = 0;
        while (plen < min_len && first[plen] == last[plen]) plen++;
        in->prefix_len = plen;
        if (plen <= EMBED_PREFIX_LEN) memcpy(in->embedded_prefix, first, plen);
    }
    for (int i = 0; i < in->header.num_items; i++)
        for (int j = 0; j < FEATURE_SIZE; j++)
            in->features[j][i] = getFeatureByte(in->anchors[i], in->prefix_len, j);
}

/* Build a filler subtree. depth 0 = leaf(MIN_FILL), depth N = inner with
 * MIN_FILL filler children at depth N-1. */
static node *buildFiller(int depth, const std::string &prefix, leafNode **leaf_tail) {
    if (depth == 0) return buildLeaf(leaf(), prefix, leaf_tail);
    return buildSubtree(n(depth, MIN_FILL), prefix, leaf_tail);
}

static node *buildInner(const InnerSpec &spec, const std::string &prefix, leafNode **leaf_tail) {
    innerNode *in = (innerNode *)zmalloc(sizeof(innerNode));
    memset(in, 0, sizeof(innerNode));
    in->header.is_leaf = false;
    in->header.num_items = (uint8_t)spec.total_children;

    int explicit_count = (int)spec.explicit_children.size();
    for (int i = 0; i < spec.total_children; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", i);
        std::string child_prefix = prefix + buf;
        if (i < explicit_count) {
            in->children[i] = buildSubtree(spec.explicit_children[i], child_prefix, leaf_tail);
        } else {
            in->children[i] = buildFiller(spec.depth - 1, child_prefix, leaf_tail);
        }
    }
    finalizeInner(in);
    return (node *)in;
}

static node *buildSubtree(const ChildSpec &spec, const std::string &prefix,
                           leafNode **leaf_tail) {
    if (std::holds_alternative<LeafSpec>(spec))
        return buildLeaf(std::get<LeafSpec>(spec), prefix, leaf_tail);
    return buildInner(std::get<InnerSpec>(spec), prefix, leaf_tail);
}

static fbtreeIndex *buildTree(const InnerSpec &root_spec) {
    fbtreeIndex *fbt = (fbtreeIndex *)zmalloc(sizeof(fbtreeIndex));
    memset(fbt, 0, sizeof(fbtreeIndex));
    leafNode *leaf_tail = nullptr;
    fbt->root = buildInner(root_spec, "", &leaf_tail);
    node *leftmost = fbt->root;
    while (!leftmost->is_leaf) leftmost = ((innerNode *)leftmost)->children[0];
    fbt->leftmost_leaf = (leafNode *)leftmost;
    fbt->rightmost_leaf = leaf_tail;
    return fbt;
}

/* Compute tree height (1 = single leaf, 2 = root + leaves, etc.) */
static int treeHeight(fbtreeIndex *fbt) {
    int h = 1;
    node *cur = fbt->root;
    while (cur && !cur->is_leaf) { h++; cur = ((innerNode *)cur)->children[0]; }
    return h;
}

/* ========== Test Fixture ========== */

class FbtreeInternalTest : public ::testing::Test {
  protected:
    size_t mem_before = 0;
    void SetUp() override { mem_before = zmalloc_used_memory(); }
    void TearDown() override {
        EXPECT_EQ(zmalloc_used_memory(), mem_before) << "Memory leak detected";
    }
};

/* ========== Builder Sanity Tests ========== */

TEST_F(FbtreeInternalTest, BuilderTwoLevelValid) {
    fbtreeIndex *fbt = buildTree(n(1, 3, {leaf(20), leaf(30), leaf(15)}));
    EXPECT_TRUE(fbtreeDebugValidate(fbt, false));
    EXPECT_EQ(fbtreeLength(fbt), 65u);
    EXPECT_EQ(treeHeight(fbt), 2);
    fbtreeFree(fbt);
}

TEST_F(FbtreeInternalTest, BuilderThreeLevelValid) {
    fbtreeIndex *fbt = buildTree(n(2, 2, {
        n(1, 2, {leaf(20), leaf(30)}),
        n(1, 2, {leaf(25), leaf(25)}),
    }));
    EXPECT_TRUE(fbtreeDebugValidate(fbt, false));
    EXPECT_EQ(fbtreeLength(fbt), 100u);
    EXPECT_EQ(treeHeight(fbt), 3);
    fbtreeFree(fbt);
}

TEST_F(FbtreeInternalTest, BuilderAutoFill) {
    /* n(1, 5) = 5 leaf children, all auto-filled with MIN_FILL items */
    fbtreeIndex *fbt = buildTree(n(1, 5));
    EXPECT_TRUE(fbtreeDebugValidate(fbt, false));
    EXPECT_EQ(fbtreeLength(fbt), (unsigned long)(5 * MIN_FILL));
    EXPECT_EQ(treeHeight(fbt), 2);
    fbtreeFree(fbt);
}

TEST_F(FbtreeInternalTest, BuilderDeepAutoFill) {
    /* n(3, 2) = depth 3, 2 children, all auto-filled */
    fbtreeIndex *fbt = buildTree(n(3, 2));
    EXPECT_TRUE(fbtreeDebugValidate(fbt, false));
    EXPECT_EQ(treeHeight(fbt), 4);
    fbtreeFree(fbt);
}

TEST_F(FbtreeInternalTest, BuilderSortedOrder) {
    fbtreeIndex *fbt = buildTree(n(1, 3, {leaf(5), leaf(5), leaf(5)}));
    EXPECT_TRUE(fbtreeDebugValidate(fbt, false));
    fbtreeIterator it;
    fbtreeInitIterator(&it, fbt);
    const_sds pos;
    const_sds prev = nullptr;
    int count = 0;
    while (fbtreeNext(&it, &pos)) {
        if (prev) {
            EXPECT_LT(sdscmp(prev, pos), 0) << "Keys not sorted at position " << count;
        }
        prev = pos;
        count++;
    }
    EXPECT_EQ(count, 15);
    fbtreeFree(fbt);
}

/* ========== Merge Cascade Tests ========== */

/* B's first child is leaf(1). Deleting its item empties the leaf, B drops
 * to MIN_FILL-1 children, underflows, and merges into A. The cascade should
 * merge the newly-adjacent boundary children. */
TEST_F(FbtreeInternalTest, MergeCascadeAtBoundary) {
    fbtreeIndex *fbt = buildTree(n(1, 3, {
        n(1, MIN_FILL),                     /* A: all filler */
        n(1, MIN_FILL, {leaf(1)}),           /* B: first child tiny, rest filler */
        n(1, MIN_FILL),                      /* C: healthy filler */
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    innerNode *root = (innerNode *)fbt->root;
    innerNode *node_b = (innerNode *)root->children[1];
    leafNode *b_leaf = (leafNode *)node_b->children[0];
    sds item_to_delete = b_leaf->values[0];

    ASSERT_TRUE(fbtreeDelete(fbt, item_to_delete));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));
    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt))
        << "Merge cascade at boundary failed (violation at depth "
        << fbtreeDebugValidateMergeEnforcementDepth(fbt) << ")";
    fbtreeFree(fbt);
}

/* Multi-level recursive merge cascade (5-level tree).
 *
 * Sizes:
 *   UNDERFLOW  = MIN_FILL - 1 = 14
 *   ALMOST     = MIN_FILL     = 15
 *   CANT_MERGE = NODE_SIZE - MIN_FILL + 2 = 48  (14 + 48 = 62 > NODE_SIZE)
 *
 * Structure:
 *   root (depth 4, 2 children)
 *   +-- A: n(3, ALMOST)  -- all filler, healthy
 *   +-- B: n(3, ALMOST, {...})
 *       +-- B[0]: n(2, CANT_MERGE)  -- too big to merge
 *       +-- B[1]: n(2, UNDERFLOW, {n(1, CANT_MERGE), n(1, UNDERFLOW)})
 *       +-- B[2]: n(2, UNDERFLOW, {n(1, UNDERFLOW), n(1, CANT_MERGE)})
 *       +-- B[3]: n(2, CANT_MERGE)  -- too big to merge
 *       +-- (filler x 11)
 *
 * Delete from B[1]'s underflow inner triggers:
 *   1. Leaf underflows -> merges with sibling inside B[1]'s underflow inner
 *   2. B[1]'s underflow inner drops to 13 -> merges with B[1]'s cant-merge (13+48=61)
 *   3. B[1] drops to 13 -> merges with B[2] (13+14=27)
 *      -> cascade: boundary underflow inners merge (13+14=27)
 *      -> cascade: boundary leaves inside merged inner merge
 *   4. B drops to 14 -> merges with A (14+15=29)
 *   5. Root collapses */
TEST_F(FbtreeInternalTest, MultiLevelRecursiveMergeCascade) {
    const int UNDERFLOW = MIN_FILL - 1;
    const int ALMOST = MIN_FILL;
    const int CANT_MERGE = NODE_SIZE - MIN_FILL + 2;

    fbtreeIndex *fbt = buildTree(n(4, 2, {
        n(3, ALMOST),
        n(3, ALMOST, {
            n(2, CANT_MERGE),
            n(2, UNDERFLOW, {
                n(1, CANT_MERGE),
                n(1, UNDERFLOW)
            }),
            n(2, UNDERFLOW, {
                n(1, UNDERFLOW),
                n(1, CANT_MERGE)
            }),
            n(2, CANT_MERGE),
        }),
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    unsigned long len_before = fbtreeLength(fbt);
    ASSERT_EQ(treeHeight(fbt), 5);

    /* Delete from B[1]'s underflow inner (last explicit child of B[1]) */
    innerNode *root = (innerNode *)fbt->root;
    innerNode *B = (innerNode *)root->children[1];
    innerNode *B1 = (innerNode *)B->children[1];
    innerNode *B1_underflow = (innerNode *)B1->children[B1->header.num_items - 1];
    leafNode *target_leaf = (leafNode *)B1_underflow->children[0];
    sds item_to_delete = target_leaf->values[0];

    ASSERT_TRUE(fbtreeDelete(fbt, item_to_delete));
    EXPECT_EQ(fbtreeLength(fbt), len_before - 1);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt))
        << "Multi-level cascade failed (violation at depth "
        << fbtreeDebugValidateMergeEnforcementDepth(fbt) << ")";

    EXPECT_EQ(treeHeight(fbt), 4) << "Expected tree to collapse by one level";

    fbtreeFree(fbt);
}

/* Scenario: tryMergeChild merges two children, shrinking the parent by 1.
 * The parent does not underflow, but its sibling was pre-existing underflowed
 * and can now merge with the smaller parent.
 *
 * Structure (3-level tree):
 *   root (depth 2, 2 children)
 *   +-- A: n(1, UNDERFLOW)       -- underflowed, can't merge with B (too full)
 *   +-- B: n(1, CANT_MERGE, {leaf(1)})  -- has a tiny leaf that will be deleted
 *
 * Delete the single item in B's tiny leaf. B loses a child via merge, dropping
 * to CANT_MERGE - 1. Now UNDERFLOW + (CANT_MERGE - 1) = 61 <= NODE_SIZE. */
TEST_F(FbtreeInternalTest, SingleDeleteParentShrinkEnablesSiblingMerge) {
    const int UNDERFLOW = MIN_FILL - 1;
    const int CANT_MERGE = NODE_SIZE - MIN_FILL + 2;

    fbtreeIndex *fbt = buildTree(n(2, 2, {
        n(1, UNDERFLOW),              /* A: underflowed sibling */
        n(1, CANT_MERGE, {leaf(1)}),  /* B: has tiny leaf, too full to merge with A */
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    innerNode *root = (innerNode *)fbt->root;
    EXPECT_GT(UNDERFLOW + CANT_MERGE, NODE_SIZE);

    /* Delete the single item in B's tiny leaf */
    innerNode *B = (innerNode *)root->children[1];
    leafNode *tiny = (leafNode *)B->children[0];
    sds item = tiny->values[0];

    ASSERT_TRUE(fbtreeDelete(fbt, item));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt))
        << "Sibling A should have merged with shrunken B after single delete "
        << "(violation at depth " << fbtreeDebugValidateMergeEnforcementDepth(fbt) << ")";

    fbtreeFree(fbt);
}

/* ========== Range Delete Tests on Constructed Trees ========== */

/* Range delete across two children of the split node in a 2-level tree.
 * This is the simplest diverged-path case. */
TEST_F(FbtreeInternalTest, RangeDeleteSimpleTwoLevel) {
    /* 5 leaves, each with 20 items = 100 total */
    fbtreeIndex *fbt = buildTree(n(1, 5, {leaf(20), leaf(20), leaf(20), leaf(20), leaf(20)}));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));
    unsigned long len = fbtreeLength(fbt);

    /* Delete a range spanning children 1-3 (middle portion) */
    unsigned long start = 25; /* middle of child 1 */
    unsigned long end = 75;   /* middle of child 3 */
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(deleted, end - start + 1);
    EXPECT_EQ(fbtreeLength(fbt), len - deleted);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    fbtreeFree(fbt);
}

/* Range delete in a 3-level tree: boundaries in different subtrees of the
 * split node, each with their own sub-path. */
TEST_F(FbtreeInternalTest, RangeDeleteThreeLevel) {

    fbtreeIndex *fbt = buildTree(n(2, 3, {
        n(1, 4, {leaf(20), leaf(20), leaf(20), leaf(20)}),
        n(1, 4, {leaf(20), leaf(20), leaf(20), leaf(20)}),
        n(1, 4, {leaf(20), leaf(20), leaf(20), leaf(20)}),
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));
    unsigned long len = fbtreeLength(fbt);

    /* Delete range spanning from middle of first subtree to middle of last */
    unsigned long start = 30;
    unsigned long end = len - 31;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(deleted, end - start + 1);
    EXPECT_EQ(fbtreeLength(fbt), len - deleted);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    fbtreeFree(fbt);
}

/* Range delete that kills entire boundary leaves (start_leaf_dies and
 * end_leaf_dies both true). */
TEST_F(FbtreeInternalTest, RangeDeleteBoundaryLeavesDie) {

    fbtreeIndex *fbt = buildTree(n(1, 5, {leaf(20), leaf(20), leaf(20), leaf(20), leaf(20)}));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));
    unsigned long len = fbtreeLength(fbt);

    /* Delete exactly children 1 through 3 (all items in those leaves) */
    unsigned long start = 20;  /* first item of child 1 */
    unsigned long end = 79;    /* last item of child 3 */
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(deleted, 60u);
    EXPECT_EQ(fbtreeLength(fbt), len - deleted);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    fbtreeFree(fbt);
}

/* Range delete in a 3-level tree where the left boundary child becomes empty
 * after trimming. Tests the child_sizes[ci] == 0 path in left subtree fixup. */
TEST_F(FbtreeInternalTest, RangeDeleteLeftBoundaryEmpty) {

    fbtreeIndex *fbt = buildTree(n(2, 3, {
        n(1, 3, {leaf(20), leaf(20), leaf(1)}),  /* last leaf has 1 item - will die */
        n(1, 3, {leaf(20), leaf(20), leaf(20)}),
        n(1, 3, {leaf(1), leaf(20), leaf(20)}),   /* first leaf has 1 item - will die */
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));
    unsigned long len = fbtreeLength(fbt);

    /* Delete range starting from the single-item leaf in child 0 through
     * the single-item leaf in child 2 */
    unsigned long start = 40;  /* the single item in child 0's last leaf */
    unsigned long end = len - 41;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(fbtreeLength(fbt), len - deleted);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    fbtreeFree(fbt);
}

/* Range delete in a 4-level tree with deep sub-paths. */
TEST_F(FbtreeInternalTest, RangeDeleteFourLevel) {

    fbtreeIndex *fbt = buildTree(n(3, 2, {
        n(2, 3, {
            n(1, 3, {leaf(20), leaf(20), leaf(20)}),
            n(1, 3, {leaf(20), leaf(20), leaf(20)}),
            n(1, 3, {leaf(20), leaf(20), leaf(20)}),
        }),
        n(2, 3, {
            n(1, 3, {leaf(20), leaf(20), leaf(20)}),
            n(1, 3, {leaf(20), leaf(20), leaf(20)}),
            n(1, 3, {leaf(20), leaf(20), leaf(20)}),
        }),
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));
    unsigned long len = fbtreeLength(fbt);

    /* Delete a large middle range */
    unsigned long start = 50;
    unsigned long end = len - 51;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(fbtreeLength(fbt), len - deleted);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    fbtreeFree(fbt);
}

/* Range delete that removes everything except a few items on each side.
 * This maximizes the amount of fixup work. */
TEST_F(FbtreeInternalTest, RangeDeleteAlmostEverything) {

    fbtreeIndex *fbt = buildTree(n(2, 4, {
        n(1, 4, {leaf(20), leaf(20), leaf(20), leaf(20)}),
        n(1, 4, {leaf(20), leaf(20), leaf(20), leaf(20)}),
        n(1, 4, {leaf(20), leaf(20), leaf(20), leaf(20)}),
        n(1, 4, {leaf(20), leaf(20), leaf(20), leaf(20)}),
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));
    unsigned long len = fbtreeLength(fbt);

    /* Delete everything except first 5 and last 5 */
    unsigned long start = 5;
    unsigned long end = len - 6;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(fbtreeLength(fbt), len - deleted);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    fbtreeFree(fbt);
}

/* Exhaustive range delete test: try many different range boundaries on a
 * fixed tree shape. This is the most likely to find off-by-one errors. */
TEST_F(FbtreeInternalTest, RangeDeleteExhaustive) {

    /* Use a small tree so we can try many ranges */
    for (unsigned long start = 0; start < 60; start += 7) {
        for (unsigned long end = start; end < 60; end += 7) {
            fbtreeIndex *fbt = buildTree(n(1, 4, {leaf(15), leaf(15), leaf(15), leaf(15)}));
            unsigned long len = fbtreeLength(fbt);
            if (end >= len) {
                fbtreeFree(fbt);
                continue;
            }
            ASSERT_TRUE(fbtreeDebugValidate(fbt, false))
                << "Initial validation failed for range [" << start << ", " << end << "]";

            unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
            EXPECT_EQ(deleted, end - start + 1)
                << "Wrong delete count for range [" << start << ", " << end << "]";
            EXPECT_EQ(fbtreeLength(fbt), len - deleted)
                << "Wrong length after range [" << start << ", " << end << "]";
            ASSERT_TRUE(fbtreeDebugValidate(fbt, false))
                << "Post-delete validation failed for range [" << start << ", " << end << "]";

            fbtreeFree(fbt);
        }
    }

}

/* Exhaustive range delete on a 3-level tree */
TEST_F(FbtreeInternalTest, RangeDeleteExhaustiveThreeLevel) {

    for (unsigned long start = 0; start < 90; start += 11) {
        for (unsigned long end = start; end < 90; end += 11) {
            fbtreeIndex *fbt = buildTree(n(2, 3, {
                n(1, 2, {leaf(15), leaf(15)}),
                n(1, 2, {leaf(15), leaf(15)}),
                n(1, 2, {leaf(15), leaf(15)}),
            }));
            unsigned long len = fbtreeLength(fbt);
            if (end >= len) {
                fbtreeFree(fbt);
                continue;
            }

            unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
            EXPECT_EQ(deleted, end - start + 1)
                << "Wrong delete count for range [" << start << ", " << end << "]";
            EXPECT_EQ(fbtreeLength(fbt), len - deleted)
                << "Wrong length after range [" << start << ", " << end << "]";
            ASSERT_TRUE(fbtreeDebugValidate(fbt, false))
                << "Post-delete validation failed for range [" << start << ", " << end << "]";

            fbtreeFree(fbt);
        }
    }

}

/* Build a tree by inserting N sequential items, then do range deletes.
 * This creates natural tree shapes from the insert path. */
TEST_F(FbtreeInternalTest, RangeDeleteInsertedTree) {

    /* Build a tree with enough items to get 3+ levels */
    int N = NODE_SIZE * NODE_SIZE + 100; /* enough for 3 levels */

    /* Try various range sizes */
    unsigned long len = (unsigned long)N;
    unsigned long ranges[][2] = {
        {10, 20},
        {0, 50},
        {len/4, 3*len/4},
        {len/2 - 10, len/2 + 10},
        {1, len - 2},
    };

    for (auto &r : ranges) {
        fbtreeIndex *fbt = fbtreeCreate();
        for (int i = 0; i < N; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%08d", i);
            sds key = sdsnew(buf);
            fbtreeInsert(fbt, key);
        }
        ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

        unsigned long start = r[0], end = r[1];
        if (end >= (unsigned long)N) end = N - 1;
        unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
        EXPECT_EQ(deleted, end - start + 1)
            << "Wrong delete count for range [" << start << ", " << end << "]";
        ASSERT_TRUE(fbtreeDebugValidate(fbt, false))
            << "Post-delete validation failed for range [" << start << ", " << end << "]";

        fbtreeFree(fbt);
    }

}

/* Test with asymmetric tree shapes where left and right sub-paths have
 * different depths. This can happen when the tree has uneven fill. */
TEST_F(FbtreeInternalTest, RangeDeleteAsymmetricSubPaths) {

    /* Left child is deep (3 levels), right child is shallow (2 levels) won't work
     * because all children of an inner node must be at the same depth.
     * Instead, create asymmetric fill patterns. */
    fbtreeIndex *fbt = buildTree(n(2, 4, {
        n(1, MIN_FILL, {leaf(1)}),   /* first child has a tiny leaf */
        n(1, MIN_FILL + 5),          /* second child is healthy */
        n(1, MIN_FILL + 5),          /* third child is healthy */
        n(1, MIN_FILL, {leaf(1)}),   /* last child has a tiny leaf */
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));
    unsigned long len = fbtreeLength(fbt);

    /* Delete range that spans from the tiny leaf area to the other tiny leaf area */
    unsigned long start = 1;
    unsigned long end = len - 2;
    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(fbtreeLength(fbt), len - deleted);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    fbtreeFree(fbt);
}

/* Replicate the property test's random tree + range delete pattern with
 * pre-merge validation enabled. Uses the same seed/key generation as
 * FbtreeTest.DeleteRangeByRankPropertyCorrectness. */
TEST_F(FbtreeInternalTest, RangeDeletePropertyReplay) {

    const int NUM_ITERATIONS = 200;
    unsigned int seed = 42;
    int key_counter = 0;

    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        int N;
        if (iter < 30) {
            N = 1 + (rand_r(&seed) % 10);
        } else if (iter < 80) {
            N = 10 + (rand_r(&seed) % 500);
        } else {
            N = 500 + (rand_r(&seed) % 4500);
        }

        fbtreeIndex *fbt = fbtreeCreate();
        for (int i = 0; i < N; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "rp_%08d", key_counter++);
            /* createString includes null terminator in sds length */
            size_t len = strlen(buf) + 1;
            sds key = sdsnewlen(buf, len);
            fbtreeInsert(fbt, key);
        }

        unsigned long start_rank = rand_r(&seed) % N;
        unsigned long end_rank = start_rank + (rand_r(&seed) % (N - start_rank));

        unsigned long len = fbtreeLength(fbt);
        unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start_rank, end_rank, NULL, NULL);
        EXPECT_EQ(deleted, end_rank - start_rank + 1)
            << "iter=" << iter << " N=" << N << " range=[" << start_rank << "," << end_rank << "]";
        EXPECT_EQ(fbtreeLength(fbt), len - deleted) << "iter=" << iter;
        EXPECT_TRUE(fbtreeDebugValidate(fbt, false))
            << "Post-delete validation failed at iter=" << iter
            << " N=" << N << " range=[" << start_rank << "," << end_rank << "]";

        fbtreeFree(fbt);
    }

}

/* Scenario: a boundary node on the leg path shrinks (loses children to the
 * range delete) but does not itself underflow. Its sibling farther from the
 * deleted range was pre-existing underflowed but could not merge because the
 * boundary node was too full. After the boundary node shrinks, the pair now
 * fits in NODE_SIZE and should be merged. mergeLeg must check siblings of
 * boundary nodes at each leg level, not just the boundary node itself.
 *
 * Structure (4-level tree, split at root):
 *   root (depth 3, 2 children)
 *   +-- L: n(2, 2, {A, B})   -- left subtree (top of left leg)
 *   |   +-- A: n(1, UNDERFLOW)   -- underflowed, cannot merge with B (too full)
 *   |   +-- B: n(1, CANT_MERGE)  -- boundary inner, will shrink
 *   +-- R: n(2, MIN_FILL)        -- right subtree, partly in deleted range
 *
 * UNDERFLOW = MIN_FILL - 1 = 14
 * CANT_MERGE = NODE_SIZE - MIN_FILL + 2 = 48 (so 14 + 48 = 62 > NODE_SIZE)
 *
 * Range delete removes the rightmost leaf of B and all of R. B drops to 47.
 * Now 14 + 47 = 61 <= NODE_SIZE, so A can merge with B. */
TEST_F(FbtreeInternalTest, RangeDeleteSiblingBecomesNewlyMergeable) {
    const int UNDERFLOW = MIN_FILL - 1;
    const int CANT_MERGE = NODE_SIZE - MIN_FILL + 2; /* UNDERFLOW + CANT_MERGE = 62 > 61 */

    /* 4-level tree. The underflowed sibling (A) is inside the top-of-left-leg
     * node (L), not a direct child of the split node. */
    fbtreeIndex *fbt = buildTree(n(3, 2, {
        n(2, 2, {
            n(1, UNDERFLOW),   /* A: underflowed sibling */
            n(1, CANT_MERGE),  /* B: boundary, too full to merge with A */
        }),
        n(2, MIN_FILL),        /* R: right subtree */
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    /* Verify A can't merge with B before the delete */
    innerNode *root = (innerNode *)fbt->root;
    innerNode *L = (innerNode *)root->children[0];
    EXPECT_EQ(L->children[0]->num_items, UNDERFLOW);
    EXPECT_EQ(L->children[1]->num_items, CANT_MERGE);
    EXPECT_GT(UNDERFLOW + CANT_MERGE, NODE_SIZE)
        << "A + B should NOT fit before delete";

    unsigned long len = fbtreeLength(fbt);

    /* Delete: rightmost leaf of B + all of R.
     * B drops to CANT_MERGE - 1 = 47. Now UNDERFLOW + 47 = 61 <= NODE_SIZE. */
    unsigned long r_size = root->child_sizes[1];
    unsigned long start = len - r_size - MIN_FILL;
    unsigned long end = len - 1;

    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(deleted, end - start + 1);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt))
        << "Sibling A should have merged with shrunken B (violation at depth "
        << fbtreeDebugValidateMergeEnforcementDepth(fbt) << ")";

    fbtreeFree(fbt);
}

/* ========== Sibling-Becomes-Mergeable Tests ========== */
/* When a node shrinks (from range delete truncation or child merge) without
 * underflowing, a pre-existing underflowed sibling may now fit and should be
 * merged. These tests cover this scenario at each position in the tree. */

/* Scenario: range delete shrinks the top-of-leg node (a child of the split
 * node) without underflowing it. The top-of-leg node's sibling at the split
 * level was pre-existing underflowed and can now merge with it.
 *
 * Structure (3-level tree, split at root):
 *   root (depth 2, 3 children)
 *   +-- A: n(1, UNDERFLOW)   -- underflowed sibling at split level
 *   +-- B: n(1, CANT_MERGE)  -- top of left leg, will shrink
 *   +-- C: n(1, MIN_FILL)    -- right subtree, fully in deleted range
 *
 * Range delete removes all of C and some of B's leaves. B shrinks,
 * making A + B fit in NODE_SIZE. */
TEST_F(FbtreeInternalTest, RangeDeleteCrotchSiblingMerge) {
    const int UNDERFLOW = MIN_FILL - 1;
    const int CANT_MERGE = NODE_SIZE - MIN_FILL + 2;

    fbtreeIndex *fbt = buildTree(n(2, 3, {
        n(1, UNDERFLOW),     /* A: underflowed sibling at split level */
        n(1, CANT_MERGE),    /* B: top of left leg, too full to merge with A */
        n(1, MIN_FILL),      /* C: will be fully deleted */
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    innerNode *root = (innerNode *)fbt->root;
    EXPECT_GT(UNDERFLOW + CANT_MERGE, NODE_SIZE);

    unsigned long len = fbtreeLength(fbt);
    unsigned long c_size = root->child_sizes[2];
    unsigned long start = len - c_size - MIN_FILL;
    unsigned long end = len - 1;

    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(deleted, end - start + 1);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt))
        << "Sibling A should have merged with shrunken B at crotch level "
        << "(violation at depth " << fbtreeDebugValidateMergeEnforcementDepth(fbt) << ")";

    fbtreeFree(fbt);
}

/* Scenario: range delete shrinks a node on the shared path without
 * underflowing it. Its sibling (at the same level, under the grandparent)
 * was pre-existing underflowed and can now merge with it.
 *
 * Structure (4-level tree):
 *   grandparent (depth 3, 2 children)
 *   +-- A: n(2, UNDERFLOW)   -- underflowed sibling on shared path
 *   +-- B: n(2, CANT_MERGE, {...})  -- shared path node, will shrink
 *       +-- B0: n(1, MIN_FILL)  -- left child of split
 *       +-- B1: n(1, MIN_FILL)  -- right child of split, partly deleted
 *
 * Range delete removes items from B1, causing merges that shrink B.
 * Now A + B fit in NODE_SIZE. */
TEST_F(FbtreeInternalTest, RangeDeleteSharedPathSiblingMerge) {
    const int UNDERFLOW = MIN_FILL - 1;
    const int CANT_MERGE = NODE_SIZE - MIN_FILL + 2;

    fbtreeIndex *fbt = buildTree(n(3, 2, {
        n(2, UNDERFLOW),                          /* A: underflowed sibling */
        n(2, CANT_MERGE, {                        /* B: shared path, too full */
            n(1, MIN_FILL),                       /* B0: left of split */
            n(1, MIN_FILL),                       /* B1: right of split */
        }),
    }));
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    innerNode *root = (innerNode *)fbt->root;
    EXPECT_GT(UNDERFLOW + CANT_MERGE, NODE_SIZE);

    unsigned long len = fbtreeLength(fbt);

    /* Delete most of B1 — enough to cause merges that shrink B by at least 1 */
    unsigned long b1_size = ((innerNode *)root->children[1])->child_sizes[
        ((innerNode *)root->children[1])->header.num_items - 1];
    unsigned long start = len - b1_size + 1; /* keep 1 item in B1 */
    unsigned long end = len - 1;

    unsigned long deleted = fbtreeDeleteRangeByRank(fbt, start, end, NULL, NULL);
    EXPECT_EQ(deleted, end - start + 1);
    ASSERT_TRUE(fbtreeDebugValidate(fbt, false));

    EXPECT_TRUE(fbtreeDebugValidateMergeEnforcement(fbt))
        << "Sibling A should have merged with shrunken B on shared path "
        << "(violation at depth " << fbtreeDebugValidateMergeEnforcementDepth(fbt) << ")";

    fbtreeFree(fbt);
}
