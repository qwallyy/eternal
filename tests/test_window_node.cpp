// Task 102: Unit tests for Window Node Tree
//
// Tests tree creation, insertion, removal, split operations, traversal,
// geometry calculation, swap operations, and serialization.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "eternal/layout/WindowNode.hpp"

using namespace eternal;

// ---------------------------------------------------------------------------
// Helper: create fake Surface pointers. We never dereference them -- they are
// used purely as opaque identity tokens for the tree.
// ---------------------------------------------------------------------------

static Surface* fakeSurface(int id) {
    return reinterpret_cast<Surface*>(static_cast<uintptr_t>(0x1000 + id));
}

// ===== Test case 1: Leaf node creation ====================================

TEST_CASE("WindowNode: leaf node creation", "[windownode]") {
    auto leaf = makeLeafNode(fakeSurface(1));
    REQUIRE(leaf != nullptr);
    CHECK(leaf->isLeaf());
    CHECK_FALSE(leaf->isContainer());
    CHECK_FALSE(leaf->isTabGroup());
    CHECK(leaf->getSurface() == fakeSurface(1));
    CHECK(leaf->getParent() == nullptr);
    CHECK(leaf->childCount() == 0);
}

// ===== Test case 2: Container node creation ===============================

TEST_CASE("WindowNode: container node creation", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal, 0.6f);
    REQUIRE(container != nullptr);
    CHECK(container->isContainer());
    CHECK(container->getSplitDir() == SplitDir::Horizontal);
    CHECK(container->getSplitRatio() == Catch::Approx(0.6f));
    CHECK(container->childCount() == 0);
}

// ===== Test case 3: Tab group creation ====================================

TEST_CASE("WindowNode: tab group creation", "[windownode]") {
    auto tabGroup = makeTabGroupNode();
    REQUIRE(tabGroup != nullptr);
    CHECK(tabGroup->isTabGroup());
    CHECK(tabGroup->getTabSurfaces().empty());
    CHECK(tabGroup->getActiveTab() == 0);
}

// ===== Test case 4: addChild and parent linkage ===========================

TEST_CASE("WindowNode: addChild sets parent", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal);
    auto child1 = makeLeafNode(fakeSurface(1));
    auto child2 = makeLeafNode(fakeSurface(2));

    auto* rawChild1 = child1.get();
    auto* rawChild2 = child2.get();

    container->addChild(std::move(child1));
    container->addChild(std::move(child2));

    CHECK(container->childCount() == 2);
    CHECK(rawChild1->getParent() == container.get());
    CHECK(rawChild2->getParent() == container.get());
    CHECK(container->childAt(0) == rawChild1);
    CHECK(container->childAt(1) == rawChild2);
}

// ===== Test case 5: insertChild at index ==================================

TEST_CASE("WindowNode: insertChild at index", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Vertical);
    container->addChild(makeLeafNode(fakeSurface(1)));
    container->addChild(makeLeafNode(fakeSurface(3)));

    auto middle = makeLeafNode(fakeSurface(2));
    auto* rawMiddle = middle.get();
    container->insertChild(1, std::move(middle));

    CHECK(container->childCount() == 3);
    CHECK(container->childAt(1) == rawMiddle);
    CHECK(container->childAt(1)->getSurface() == fakeSurface(2));
}

// ===== Test case 6: removeChild by pointer ================================

TEST_CASE("WindowNode: removeChild by pointer", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal);
    auto child1 = makeLeafNode(fakeSurface(1));
    auto child2 = makeLeafNode(fakeSurface(2));
    auto* rawChild1 = child1.get();

    container->addChild(std::move(child1));
    container->addChild(std::move(child2));

    auto removed = container->removeChild(rawChild1);
    REQUIRE(removed != nullptr);
    CHECK(removed->getSurface() == fakeSurface(1));
    CHECK(removed->getParent() == nullptr);
    CHECK(container->childCount() == 1);
}

// ===== Test case 7: removeChildAt by index ================================

TEST_CASE("WindowNode: removeChildAt by index", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal);
    container->addChild(makeLeafNode(fakeSurface(1)));
    container->addChild(makeLeafNode(fakeSurface(2)));
    container->addChild(makeLeafNode(fakeSurface(3)));

    auto removed = container->removeChildAt(1);
    REQUIRE(removed != nullptr);
    CHECK(removed->getSurface() == fakeSurface(2));
    CHECK(container->childCount() == 2);
    CHECK(container->childAt(0)->getSurface() == fakeSurface(1));
    CHECK(container->childAt(1)->getSurface() == fakeSurface(3));
}

// ===== Test case 8: split ratio clamping ==================================

TEST_CASE("WindowNode: split ratio is clamped", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal);
    container->setSplitRatio(0.0f);
    CHECK(container->getSplitRatio() >= 0.05f);
    container->setSplitRatio(1.0f);
    CHECK(container->getSplitRatio() <= 0.95f);
    container->setSplitRatio(0.5f);
    CHECK(container->getSplitRatio() == Catch::Approx(0.5f));
}

// ===== Test case 9: DFS findNode ==========================================

TEST_CASE("WindowNode: findNode DFS", "[windownode]") {
    auto root = makeContainerNode(SplitDir::Horizontal);
    auto left = makeContainerNode(SplitDir::Vertical);
    auto leftLeaf1 = makeLeafNode(fakeSurface(1));
    auto leftLeaf2 = makeLeafNode(fakeSurface(2));
    auto rightLeaf = makeLeafNode(fakeSurface(3));

    auto* pLeftLeaf2 = leftLeaf2.get();

    left->addChild(std::move(leftLeaf1));
    left->addChild(std::move(leftLeaf2));
    root->addChild(std::move(left));
    root->addChild(std::move(rightLeaf));

    auto* found = root->findNode(fakeSurface(2));
    REQUIRE(found != nullptr);
    CHECK(found == pLeftLeaf2);
    CHECK(found->getSurface() == fakeSurface(2));

    // Not found returns nullptr
    CHECK(root->findNode(fakeSurface(99)) == nullptr);
    CHECK(root->findNode(nullptr) == nullptr);
}

// ===== Test case 10: collectSurfaces (BFS-order collection) ===============

TEST_CASE("WindowNode: collectSurfaces traversal", "[windownode]") {
    auto root = makeContainerNode(SplitDir::Horizontal);
    auto left = makeContainerNode(SplitDir::Vertical);
    left->addChild(makeLeafNode(fakeSurface(1)));
    left->addChild(makeLeafNode(fakeSurface(2)));
    root->addChild(std::move(left));
    root->addChild(makeLeafNode(fakeSurface(3)));

    std::vector<Surface*> surfaces;
    root->collectSurfaces(surfaces);

    REQUIRE(surfaces.size() == 3);
    CHECK(surfaces[0] == fakeSurface(1));
    CHECK(surfaces[1] == fakeSurface(2));
    CHECK(surfaces[2] == fakeSurface(3));
}

// ===== Test case 11: collectLeaves ========================================

TEST_CASE("WindowNode: collectLeaves", "[windownode]") {
    auto root = makeContainerNode(SplitDir::Horizontal);
    root->addChild(makeLeafNode(fakeSurface(1)));

    auto rightContainer = makeContainerNode(SplitDir::Vertical);
    rightContainer->addChild(makeLeafNode(fakeSurface(2)));
    rightContainer->addChild(makeLeafNode(fakeSurface(3)));
    root->addChild(std::move(rightContainer));

    std::vector<WindowNode*> leaves;
    root->collectLeaves(leaves);

    REQUIRE(leaves.size() == 3);
    CHECK(leaves[0]->getSurface() == fakeSurface(1));
    CHECK(leaves[1]->getSurface() == fakeSurface(2));
    CHECK(leaves[2]->getSurface() == fakeSurface(3));
}

// ===== Test case 12: geometry calculation (binary horizontal split) =======

TEST_CASE("WindowNode: computeGeometry horizontal split", "[windownode]") {
    // We need to set surfaces to non-null values but we will not call
    // setGeometry on them (which dereferences the pointer). Override the
    // computation by using pseudo-tiled off (default). Since we're using
    // fake surfaces that we can't dereference, we test geometry on the node
    // itself rather than through surface->setGeometry.
    //
    // Actually, computeGeometry calls surface->setGeometry which would crash.
    // Instead, test the N-ary path with null surfaces (no crash on nullptr).
    auto root = makeContainerNode(SplitDir::Horizontal, 0.5f);
    auto left = makeLeafNode(nullptr);
    auto right = makeLeafNode(nullptr);

    auto* pLeft = left.get();
    auto* pRight = right.get();

    root->addChild(std::move(left));
    root->addChild(std::move(right));

    Box bounds{0, 0, 1000, 500};
    GapConfig gaps{10, 20};
    root->computeGeometry(bounds, gaps);

    // Root geometry should be the full bounds
    CHECK(root->getGeometry().x == 0);
    CHECK(root->getGeometry().y == 0);
    CHECK(root->getGeometry().width == 1000);

    // Children should divide the space horizontally with inner gap
    CHECK(pLeft->getGeometry().x == 0);
    CHECK(pLeft->getGeometry().width > 0);
    CHECK(pRight->getGeometry().x > pLeft->getGeometry().x);
    CHECK(pRight->getGeometry().width > 0);

    // The gap between left and right should be the inner gap
    int gapStart = pLeft->getGeometry().x + pLeft->getGeometry().width;
    int gapEnd = pRight->getGeometry().x;
    CHECK(gapEnd - gapStart == gaps.inner);
}

// ===== Test case 13: geometry calculation (vertical split) ================

TEST_CASE("WindowNode: computeGeometry vertical split", "[windownode]") {
    auto root = makeContainerNode(SplitDir::Vertical, 0.5f);
    root->addChild(makeLeafNode(nullptr));
    root->addChild(makeLeafNode(nullptr));

    Box bounds{0, 0, 500, 1000};
    GapConfig gaps{10, 0};
    root->computeGeometry(bounds, gaps);

    auto* top = root->childAt(0);
    auto* bot = root->childAt(1);

    CHECK(top->getGeometry().y == 0);
    CHECK(bot->getGeometry().y > top->getGeometry().y);

    int gapStart = top->getGeometry().y + top->getGeometry().height;
    int gapEnd = bot->getGeometry().y;
    CHECK(gapEnd - gapStart == gaps.inner);
}

// ===== Test case 14: swap two leaf nodes ==================================

TEST_CASE("WindowNode: swap leaf nodes", "[windownode]") {
    auto leaf1 = makeLeafNode(fakeSurface(1));
    auto leaf2 = makeLeafNode(fakeSurface(2));

    WindowNode::swap(leaf1.get(), leaf2.get());

    CHECK(leaf1->getSurface() == fakeSurface(2));
    CHECK(leaf2->getSurface() == fakeSurface(1));
}

// ===== Test case 15: serialize and deserialize ============================

TEST_CASE("WindowNode: serialize and deserialize roundtrip", "[windownode]") {
    // Build a tree:
    //        container(H)
    //       /           \
    //  leaf(S1)     container(V)
    //               /        \
    //          leaf(S2)    leaf(S3)

    auto root = makeContainerNode(SplitDir::Horizontal, 0.6f);
    auto rightContainer = makeContainerNode(SplitDir::Vertical, 0.4f);
    rightContainer->addChild(makeLeafNode(fakeSurface(2)));
    rightContainer->addChild(makeLeafNode(fakeSurface(3)));
    root->addChild(makeLeafNode(fakeSurface(1)));
    root->addChild(std::move(rightContainer));

    // Serialize
    NodeState state = root->serialize();

    CHECK(state.type == NodeType::Container);
    CHECK(state.split_dir == SplitDir::Horizontal);
    CHECK(state.children.size() == 2);
    CHECK(state.children[0].type == NodeType::Leaf);
    CHECK(state.children[1].type == NodeType::Container);
    CHECK(state.children[1].children.size() == 2);

    // Deserialize with a simple lookup that returns our fake surfaces
    auto lookup = [](uint64_t id) -> Surface* {
        return reinterpret_cast<Surface*>(static_cast<uintptr_t>(id));
    };

    auto restored = WindowNode::deserialize(state, lookup);
    REQUIRE(restored != nullptr);
    CHECK(restored->isContainer());
    CHECK(restored->childCount() == 2);
    CHECK(restored->childAt(0)->isLeaf());
    CHECK(restored->childAt(1)->isContainer());
    CHECK(restored->childAt(1)->childCount() == 2);
    CHECK(restored->getSplitDir() == SplitDir::Horizontal);
    CHECK(restored->getSplitRatio() == Catch::Approx(0.6f));
}

// ===== Test case 16: detach from parent ===================================

TEST_CASE("WindowNode: detach from parent", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal);
    auto child = makeLeafNode(fakeSurface(1));
    auto* rawChild = child.get();

    container->addChild(std::move(child));
    CHECK(container->childCount() == 1);
    CHECK(rawChild->getParent() == container.get());

    auto detached = rawChild->detach();
    REQUIRE(detached != nullptr);
    CHECK(detached.get() == rawChild);
    CHECK(detached->getParent() == nullptr);
    CHECK(container->childCount() == 0);
}

// ===== Test case 17: insertAfter and insertBefore =========================

TEST_CASE("WindowNode: insertAfter and insertBefore", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal);
    container->addChild(makeLeafNode(fakeSurface(1)));
    container->addChild(makeLeafNode(fakeSurface(3)));

    auto* first = container->childAt(0);

    // Insert after first
    WindowNode::insertAfter(first, makeLeafNode(fakeSurface(2)));
    CHECK(container->childCount() == 3);
    CHECK(container->childAt(0)->getSurface() == fakeSurface(1));
    CHECK(container->childAt(1)->getSurface() == fakeSurface(2));
    CHECK(container->childAt(2)->getSurface() == fakeSurface(3));

    // Insert before first
    WindowNode::insertBefore(container->childAt(0), makeLeafNode(fakeSurface(0)));
    CHECK(container->childCount() == 4);
    CHECK(container->childAt(0)->getSurface() == fakeSurface(0));
}

// ===== Test case 18: tab group operations =================================

TEST_CASE("WindowNode: tab group add/remove/active", "[windownode]") {
    auto tabGroup = makeTabGroupNode();

    tabGroup->addTabSurface(fakeSurface(1));
    tabGroup->addTabSurface(fakeSurface(2));
    tabGroup->addTabSurface(fakeSurface(3));

    CHECK(tabGroup->getTabSurfaces().size() == 3);
    CHECK(tabGroup->getActiveTabSurface() == fakeSurface(1));

    tabGroup->setActiveTab(2);
    CHECK(tabGroup->getActiveTabSurface() == fakeSurface(3));

    tabGroup->removeTabSurface(fakeSurface(2));
    CHECK(tabGroup->getTabSurfaces().size() == 2);

    // Active tab should be clamped
    tabGroup->setActiveTab(10);
    CHECK(tabGroup->getActiveTab() <= 1);
}

// ===== Test case 19: indexInParent ========================================

TEST_CASE("WindowNode: indexInParent", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal);
    container->addChild(makeLeafNode(fakeSurface(1)));
    container->addChild(makeLeafNode(fakeSurface(2)));
    container->addChild(makeLeafNode(fakeSurface(3)));

    CHECK(container->childAt(0)->indexInParent() == 0);
    CHECK(container->childAt(1)->indexInParent() == 1);
    CHECK(container->childAt(2)->indexInParent() == 2);

    // Root node has no parent
    CHECK(container->indexInParent() == -1);
}

// ===== Test case 20: childAt out of bounds returns nullptr ================

TEST_CASE("WindowNode: childAt out of bounds", "[windownode]") {
    auto container = makeContainerNode(SplitDir::Horizontal);
    container->addChild(makeLeafNode(fakeSurface(1)));

    CHECK(container->childAt(-1) == nullptr);
    CHECK(container->childAt(1) == nullptr);
    CHECK(container->childAt(100) == nullptr);
}
