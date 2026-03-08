// Task 104: Integration tests via headless backend
//
// Uses WLR_BACKENDS=headless for headless compositor testing.
// Tests output creation, surface lifecycle, workspace switching,
// layout recalculation, and focus management.

#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
}

#include "eternal/layout/WindowNode.hpp"
#include "eternal/layout/ILayout.hpp"

using namespace eternal;

// ---------------------------------------------------------------------------
// RAII wrapper for Wayland display + wlroots headless backend
// ---------------------------------------------------------------------------

struct HeadlessFixture {
    struct wl_display* display = nullptr;
    struct wlr_backend* backend = nullptr;
    struct wlr_renderer* renderer = nullptr;
    struct wlr_allocator* allocator = nullptr;
    struct wlr_scene* scene = nullptr;

    bool initialized = false;

    HeadlessFixture() {
        wlr_log_init(WLR_ERROR, nullptr);

        display = wl_display_create();
        if (!display) return;

        // Force headless backend via environment
        setenv("WLR_BACKENDS", "headless", 1);

        backend = wlr_backend_autocreate(wl_display_get_event_loop(display), nullptr);
        if (!backend) {
            wl_display_destroy(display);
            display = nullptr;
            return;
        }

        renderer = wlr_renderer_autocreate(backend);
        if (!renderer) {
            wlr_backend_destroy(backend);
            wl_display_destroy(display);
            display = nullptr;
            backend = nullptr;
            return;
        }

        wlr_renderer_init_wl_display(renderer, display);

        allocator = wlr_allocator_autocreate(backend, renderer);
        if (!allocator) {
            wlr_backend_destroy(backend);
            wl_display_destroy(display);
            display = nullptr;
            backend = nullptr;
            renderer = nullptr;
            return;
        }

        scene = wlr_scene_create();

        initialized = true;
    }

    ~HeadlessFixture() {
        if (scene) wlr_scene_node_destroy(&scene->tree.node);
        if (backend) wlr_backend_destroy(backend);
        if (display) wl_display_destroy(display);
    }

    HeadlessFixture(const HeadlessFixture&) = delete;
    HeadlessFixture& operator=(const HeadlessFixture&) = delete;
};

// ===== Test case 1: Display creation ======================================

TEST_CASE("Headless: wl_display creation", "[headless]") {
    struct wl_display* display = wl_display_create();
    REQUIRE(display != nullptr);
    wl_display_destroy(display);
}

// ===== Test case 2: Headless backend creation =============================

TEST_CASE("Headless: backend autocreate with headless", "[headless]") {
    HeadlessFixture fixture;
    if (!fixture.initialized) {
        SKIP("Headless backend not available in this environment");
    }
    REQUIRE(fixture.backend != nullptr);
    REQUIRE(fixture.renderer != nullptr);
    REQUIRE(fixture.allocator != nullptr);
}

// ===== Test case 3: Create headless output ================================

TEST_CASE("Headless: create headless output", "[headless]") {
    HeadlessFixture fixture;
    if (!fixture.initialized) {
        SKIP("Headless backend not available in this environment");
    }

    struct wlr_output* output = wlr_headless_add_output(fixture.backend, 1920, 1080);
    REQUIRE(output != nullptr);
    CHECK(output->width == 1920);
    CHECK(output->height == 1080);
}

// ===== Test case 4: Multiple headless outputs =============================

TEST_CASE("Headless: create multiple outputs", "[headless]") {
    HeadlessFixture fixture;
    if (!fixture.initialized) {
        SKIP("Headless backend not available in this environment");
    }

    struct wlr_output* output1 = wlr_headless_add_output(fixture.backend, 1920, 1080);
    struct wlr_output* output2 = wlr_headless_add_output(fixture.backend, 2560, 1440);

    REQUIRE(output1 != nullptr);
    REQUIRE(output2 != nullptr);
    CHECK(output1 != output2);
    CHECK(output1->width == 1920);
    CHECK(output2->width == 2560);
}

// ===== Test case 5: Backend start =========================================

TEST_CASE("Headless: backend start", "[headless]") {
    HeadlessFixture fixture;
    if (!fixture.initialized) {
        SKIP("Headless backend not available in this environment");
    }

    wlr_headless_add_output(fixture.backend, 1920, 1080);

    bool started = wlr_backend_start(fixture.backend);
    CHECK(started);
}

// ===== Test case 6: Scene tree creation ===================================

TEST_CASE("Headless: scene graph creation", "[headless]") {
    HeadlessFixture fixture;
    if (!fixture.initialized) {
        SKIP("Headless backend not available in this environment");
    }

    REQUIRE(fixture.scene != nullptr);

    // Create scene tree nodes for layered rendering
    struct wlr_scene_tree* bg = wlr_scene_tree_create(&fixture.scene->tree);
    REQUIRE(bg != nullptr);

    struct wlr_scene_tree* surface_tree = wlr_scene_tree_create(&fixture.scene->tree);
    REQUIRE(surface_tree != nullptr);

    struct wlr_scene_tree* overlay = wlr_scene_tree_create(&fixture.scene->tree);
    REQUIRE(overlay != nullptr);
}

// ===== Test case 7: Layout recalculation with WindowNode ==================

TEST_CASE("Headless: layout recalculation after add/remove", "[headless]") {
    // This test uses the WindowNode tree directly (no wlroots dependency)
    // to verify layout recalculation logic as would happen in a headless env.

    auto root = makeContainerNode(SplitDir::Horizontal, 0.5f);
    root->addChild(makeLeafNode(nullptr));

    Box area{0, 0, 1920, 1080};
    GapConfig gaps{5, 10};
    root->computeGeometry(area, gaps);

    // Single child fills full area
    CHECK(root->childAt(0)->getGeometry().width == 1920);

    // Add second child
    root->addChild(makeLeafNode(nullptr));
    root->computeGeometry(area, gaps);

    // Now two children should split the space
    CHECK(root->childAt(0)->getGeometry().width > 0);
    CHECK(root->childAt(1)->getGeometry().width > 0);
    CHECK(root->childAt(0)->getGeometry().width +
          root->childAt(1)->getGeometry().width +
          gaps.inner == area.width);

    // Remove first child
    root->removeChildAt(0);
    root->computeGeometry(area, gaps);

    CHECK(root->childCount() == 1);
    CHECK(root->childAt(0)->getGeometry().width == 1920);
}

// ===== Test case 8: Workspace switching simulation ========================

TEST_CASE("Headless: workspace switching simulation", "[headless]") {
    // Simulate workspace switching by maintaining two separate node trees
    // and swapping which one is "active".

    auto ws1_root = makeContainerNode(SplitDir::Horizontal, 0.5f);
    ws1_root->addChild(makeLeafNode(nullptr));
    ws1_root->addChild(makeLeafNode(nullptr));

    auto ws2_root = makeContainerNode(SplitDir::Vertical, 0.5f);
    ws2_root->addChild(makeLeafNode(nullptr));
    ws2_root->addChild(makeLeafNode(nullptr));
    ws2_root->addChild(makeLeafNode(nullptr));

    Box area{0, 0, 1920, 1080};
    GapConfig gaps{5, 10};

    // Switch to workspace 1
    WindowNode* active = ws1_root.get();
    active->computeGeometry(area, gaps);
    CHECK(active->childCount() == 2);

    // Switch to workspace 2
    active = ws2_root.get();
    active->computeGeometry(area, gaps);
    CHECK(active->childCount() == 3);
}

// ===== Test case 9: Focus management simulation ==========================

TEST_CASE("Headless: focus management through node tree", "[headless]") {
    auto root = makeContainerNode(SplitDir::Horizontal, 0.5f);

    Surface* s1 = reinterpret_cast<Surface*>(0x1001);
    Surface* s2 = reinterpret_cast<Surface*>(0x1002);
    Surface* s3 = reinterpret_cast<Surface*>(0x1003);

    root->addChild(makeLeafNode(s1));
    auto right = makeContainerNode(SplitDir::Vertical, 0.5f);
    right->addChild(makeLeafNode(s2));
    right->addChild(makeLeafNode(s3));
    root->addChild(std::move(right));

    // Simulate focus: find node for s2
    auto* focused = root->findNode(s2);
    REQUIRE(focused != nullptr);
    CHECK(focused->getSurface() == s2);

    // Navigate to sibling
    auto* parent = focused->getParent();
    REQUIRE(parent != nullptr);
    int idx = focused->indexInParent();
    CHECK(idx == 0);

    // Focus next sibling (s3)
    if (idx + 1 < parent->childCount()) {
        auto* next = parent->childAt(idx + 1);
        CHECK(next->getSurface() == s3);
    }
}

// ===== Test case 10: Socket path generation ===============================

TEST_CASE("Headless: wayland socket can be added", "[headless]") {
    struct wl_display* display = wl_display_create();
    REQUIRE(display != nullptr);

    // wl_display_add_socket_auto returns a socket name
    const char* socket = wl_display_add_socket_auto(display);
    REQUIRE(socket != nullptr);
    CHECK(std::string(socket).length() > 0);

    wl_display_destroy(display);
}
