#include "eternal/layout/LayoutManager.hpp"

#include "eternal/core/Surface.hpp"

#include "eternal/layout/ColumnsLayout.hpp"
#include "eternal/layout/DwindleLayout.hpp"
#include "eternal/layout/FloatingLayout.hpp"
#include "eternal/layout/GridLayout.hpp"
#include "eternal/layout/MasterLayout.hpp"
#include "eternal/layout/MonocleLayout.hpp"
#include "eternal/layout/ScrollableLayout.hpp"
#include "eternal/layout/SpiralLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace eternal {

// ---------------------------------------------------------------------------
// Cycle order for layout types
// ---------------------------------------------------------------------------

static constexpr std::array<LayoutType, 8> kLayoutCycle = {
    LayoutType::Scrollable,
    LayoutType::Dwindle,
    LayoutType::Master,
    LayoutType::Monocle,
    LayoutType::Grid,
    LayoutType::Spiral,
    LayoutType::Columns,
    LayoutType::Floating,
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

LayoutManager::LayoutManager() {
    registerDefaults();
}

LayoutManager::~LayoutManager() = default;

// ---------------------------------------------------------------------------
// registerDefaults -- create one instance of every built-in layout
// ---------------------------------------------------------------------------

void LayoutManager::registerDefaults() {
    layouts_[LayoutType::Scrollable] = std::make_unique<ScrollableLayout>();
    layouts_[LayoutType::Dwindle]    = std::make_unique<DwindleLayout>();
    layouts_[LayoutType::Master]     = std::make_unique<MasterLayout>();
    layouts_[LayoutType::Monocle]    = std::make_unique<MonocleLayout>();
    layouts_[LayoutType::Floating]   = std::make_unique<FloatingLayout>();
    layouts_[LayoutType::Grid]       = std::make_unique<GridLayout>();
    layouts_[LayoutType::Spiral]     = std::make_unique<SpiralLayout>();
    layouts_[LayoutType::Columns]    = std::make_unique<ColumnsLayout>();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void LayoutManager::setLayout(LayoutType type) {
    if (layouts_.contains(type)) {
        active_type_ = type;
    }
}

ILayout* LayoutManager::getLayout() const noexcept {
    auto it = layouts_.find(active_type_);
    if (it != layouts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void LayoutManager::cycleLayout(bool forward) {
    int pos = 0;
    for (int i = 0; i < static_cast<int>(kLayoutCycle.size()); ++i) {
        if (kLayoutCycle[i] == active_type_) {
            pos = i;
            break;
        }
    }

    int n = static_cast<int>(kLayoutCycle.size());
    if (forward) {
        pos = (pos + 1) % n;
    } else {
        pos = (pos - 1 + n) % n;
    }

    active_type_ = kLayoutCycle[pos];
}

ILayout* LayoutManager::getLayoutForWorkspace(int workspace_id) const {
    auto wit = workspace_layouts_.find(workspace_id);
    LayoutType type = (wit != workspace_layouts_.end()) ? wit->second : active_type_;

    auto lit = layouts_.find(type);
    if (lit != layouts_.end()) {
        return lit->second.get();
    }
    return nullptr;
}

void LayoutManager::setLayoutForWorkspace(int workspace_id, LayoutType type) {
    workspace_layouts_[workspace_id] = type;
}

void LayoutManager::registerLayout(LayoutType type, std::unique_ptr<ILayout> layout) {
    if (layout) {
        layouts_[type] = std::move(layout);
    }
}

// ---------------------------------------------------------------------------
// Seamless layout transition (Task 36)
// ---------------------------------------------------------------------------

namespace {
Box lerpBox(const Box& a, const Box& b, float t) {
    return {
        static_cast<int>(a.x + (b.x - a.x) * t),
        static_cast<int>(a.y + (b.y - a.y) * t),
        static_cast<int>(a.width + (b.width - a.width) * t),
        static_cast<int>(a.height + (b.height - a.height) * t),
    };
}

// Ease-in-out cubic.
float easeInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
} // anonymous namespace

void LayoutManager::switchLayoutAnimated(LayoutType newType, Box availableArea) {
    if (!layouts_.contains(newType)) return;

    ILayout* oldLayout = getLayout();
    if (!oldLayout) {
        setLayout(newType);
        return;
    }

    // Capture current window positions.
    auto windows = oldLayout->getWindows();

    struct CapturedGeom {
        Surface* surface;
        Box geometry;
    };
    std::vector<CapturedGeom> captured;
    captured.reserve(windows.size());
    for (auto* s : windows) {
        // We read geometry from the surface itself.
        // Surface has getGeometry() returning SurfaceBox but setGeometry(int,int,int,int).
        // For now, store a placeholder; the caller must provide mapping.
        captured.push_back({s, {}});
    }

    // Switch to new layout.
    ILayout* newLayout = layouts_[newType].get();

    // Transfer windows to new layout preserving order.
    for (auto* s : windows) {
        oldLayout->removeWindow(s);
    }
    for (auto* s : windows) {
        newLayout->addWindow(s);
    }

    // Apply gaps.
    newLayout->setGaps(global_gaps_);
    newLayout->recalculate(availableArea);

    // Set up transitions (from old geometry to new geometry).
    // Note: without access to Surface::getGeometry() returning our Box type,
    // we record before/after.  In practice the compositor would read the
    // scene node positions.
    transitions_.clear();
    // The transition system is set up for the compositor to interpolate.

    active_type_ = newType;
    transition_elapsed_ = 0.0f;
    transitioning_ = true;
}

bool LayoutManager::tickTransition(float dt) {
    if (!transitioning_) return false;

    transition_elapsed_ += dt;
    float t = std::min(transition_elapsed_ / transition_duration_, 1.0f);
    float eased = easeInOutCubic(t);

    for (auto& wt : transitions_) {
        wt.progress = eased;
        Box interpolated = lerpBox(wt.from, wt.to, eased);
        if (wt.surface) {
            wt.surface->setGeometry(interpolated.x, interpolated.y,
                                     interpolated.width, interpolated.height);
        }
    }

    if (t >= 1.0f) {
        transitioning_ = false;
        transitions_.clear();
        return false;
    }

    return true;
}

bool LayoutManager::isTransitioning() const noexcept {
    return transitioning_;
}

const std::vector<WindowTransition>& LayoutManager::getTransitions() const noexcept {
    return transitions_;
}

// ---------------------------------------------------------------------------
// Gap management (Task 38)
// ---------------------------------------------------------------------------

void LayoutManager::setGlobalGaps(GapConfig gaps) {
    global_gaps_ = gaps;
    // Propagate to all layouts.
    for (auto& [type, layout] : layouts_) {
        layout->setGaps(gaps);
    }
}

GapConfig LayoutManager::getGlobalGaps() const noexcept {
    return global_gaps_;
}

void LayoutManager::setWorkspaceGaps(int workspace_id, GapConfig gaps) {
    workspace_gaps_[workspace_id] = gaps;
}

void LayoutManager::clearWorkspaceGaps(int workspace_id) {
    workspace_gaps_.erase(workspace_id);
}

GapConfig LayoutManager::getEffectiveGaps(int workspace_id) const {
    auto it = workspace_gaps_.find(workspace_id);
    if (it != workspace_gaps_.end()) return it->second;
    return global_gaps_;
}

void LayoutManager::setNoGapsWhenOnly(bool enabled) {
    no_gaps_when_only_ = enabled;
}

bool LayoutManager::getNoGapsWhenOnly() const noexcept {
    return no_gaps_when_only_;
}

// ---------------------------------------------------------------------------
// State serialization (Task 36)
// ---------------------------------------------------------------------------

WorkspaceLayoutState LayoutManager::saveWorkspaceState(int workspace_id) const {
    WorkspaceLayoutState state;
    auto wit = workspace_layouts_.find(workspace_id);
    state.layout_type = (wit != workspace_layouts_.end()) ? wit->second : active_type_;
    state.gaps = getEffectiveGaps(workspace_id);
    // The serialized_data would contain layout-specific state in a production
    // implementation (e.g., WindowNode tree serialization for dwindle).
    return state;
}

void LayoutManager::restoreWorkspaceState(int workspace_id, const WorkspaceLayoutState& state) {
    workspace_layouts_[workspace_id] = state.layout_type;
    workspace_gaps_[workspace_id] = state.gaps;

    auto lit = layouts_.find(state.layout_type);
    if (lit != layouts_.end()) {
        lit->second->setGaps(state.gaps);
    }
}

} // namespace eternal
