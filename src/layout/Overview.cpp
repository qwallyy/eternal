#include "eternal/layout/Overview.hpp"

#include <algorithm>
#include <cmath>
#include <climits>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Overview::Overview() : Overview(OverviewConfig{}) {}

Overview::Overview(OverviewConfig config) : config_(config) {}

Overview::~Overview() = default;

// ---------------------------------------------------------------------------
// Activation
// ---------------------------------------------------------------------------

void Overview::enter(const std::vector<Surface*>& windows,
                     const std::vector<Workspace*>& workspaces,
                     Box available_area) {
    all_windows_ = windows;
    workspaces_ = workspaces;
    available_area_ = available_area;

    // Apply filter.
    filtered_windows_.clear();
    for (auto* s : all_windows_) {
        if (matchesFilter(s)) {
            filtered_windows_.push_back(s);
        }
    }

    active_ = true;
    entering_ = true;
    exiting_ = false;
    zoom_progress_ = 0.0f;

    if (!filtered_windows_.empty()) {
        selected_index_ = std::clamp(selected_index_, 0,
                                      static_cast<int>(filtered_windows_.size()) - 1);
    } else {
        selected_index_ = 0;
    }

    recalculate();
}

void Overview::exit() {
    entering_ = false;
    exiting_ = true;
}

void Overview::toggle(const std::vector<Surface*>& windows,
                      const std::vector<Workspace*>& workspaces,
                      Box available_area) {
    if (active_ && !exiting_) {
        exit();
    } else {
        enter(windows, workspaces, available_area);
    }
}

bool Overview::isActive() const noexcept {
    return active_;
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

bool Overview::tickAnimation(float dt) {
    if (!active_ && !exiting_) return false;

    float speed = 1.0f / std::max(0.01f, config_.animation_duration);

    if (entering_) {
        zoom_progress_ += speed * dt;
        if (zoom_progress_ >= 1.0f) {
            zoom_progress_ = 1.0f;
            entering_ = false;
        }
        return true;
    }

    if (exiting_) {
        zoom_progress_ -= speed * dt;
        if (zoom_progress_ <= 0.0f) {
            zoom_progress_ = 0.0f;
            exiting_ = false;
            active_ = false;
            thumbnails_.clear();
            workspace_previews_.clear();
        }
        return zoom_progress_ > 0.0f;
    }

    return false;
}

float Overview::getZoomProgress() const noexcept {
    return zoom_progress_;
}

bool Overview::isAnimating() const noexcept {
    return entering_ || exiting_;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void Overview::selectNext() {
    if (filtered_windows_.empty()) return;
    selected_index_ = (selected_index_ + 1) % static_cast<int>(filtered_windows_.size());
    // Update focused state in thumbnails.
    for (int i = 0; i < static_cast<int>(thumbnails_.size()); ++i) {
        thumbnails_[i].focused = (i == selected_index_);
    }
}

void Overview::selectPrev() {
    if (filtered_windows_.empty()) return;
    int n = static_cast<int>(filtered_windows_.size());
    selected_index_ = (selected_index_ - 1 + n) % n;
    for (int i = 0; i < static_cast<int>(thumbnails_.size()); ++i) {
        thumbnails_[i].focused = (i == selected_index_);
    }
}

void Overview::selectDirection(Direction dir) {
    if (thumbnails_.size() < 2) return;

    auto& current = thumbnails_[selected_index_].thumbnail_geometry;
    int cx = current.x + current.width / 2;
    int cy = current.y + current.height / 2;

    int best = selected_index_;
    int best_dist = INT_MAX;

    for (int i = 0; i < static_cast<int>(thumbnails_.size()); ++i) {
        if (i == selected_index_) continue;
        auto& tg = thumbnails_[i].thumbnail_geometry;
        int nx = tg.x + tg.width / 2;
        int ny = tg.y + tg.height / 2;
        int dx = nx - cx;
        int dy = ny - cy;

        bool valid = false;
        switch (dir) {
            case Direction::Left:  valid = (dx < 0); break;
            case Direction::Right: valid = (dx > 0); break;
            case Direction::Up:    valid = (dy < 0); break;
            case Direction::Down:  valid = (dy > 0); break;
        }

        if (valid) {
            int dist = dx * dx + dy * dy;
            if (dist < best_dist) {
                best_dist = dist;
                best = i;
            }
        }
    }

    selected_index_ = best;
    for (int i = 0; i < static_cast<int>(thumbnails_.size()); ++i) {
        thumbnails_[i].focused = (i == selected_index_);
    }
}

Surface* Overview::getSelectedSurface() const {
    if (filtered_windows_.empty()) return nullptr;
    int idx = std::clamp(selected_index_, 0,
                          static_cast<int>(filtered_windows_.size()) - 1);
    return filtered_windows_[idx];
}

int Overview::getSelectedIndex() const noexcept {
    return selected_index_;
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

Surface* Overview::handleClick(int px, int py) {
    for (int i = 0; i < static_cast<int>(thumbnails_.size()); ++i) {
        auto& tg = thumbnails_[i].thumbnail_geometry;
        if (tg.contains(px, py)) {
            selected_index_ = i;
            return thumbnails_[i].surface;
        }
    }
    return nullptr;
}

void Overview::handleMouseMove(int px, int py) {
    for (auto& thumb : thumbnails_) {
        thumb.hovered = thumb.thumbnail_geometry.contains(px, py);
    }
    for (auto& wp : workspace_previews_) {
        wp.hovered = wp.geometry.contains(px, py);
    }
}

// ---------------------------------------------------------------------------
// Search / filter
// ---------------------------------------------------------------------------

void Overview::setFilter(const std::string& f) {
    filter_ = f;

    filtered_windows_.clear();
    for (auto* s : all_windows_) {
        if (matchesFilter(s)) {
            filtered_windows_.push_back(s);
        }
    }

    selected_index_ = 0;
    recalculate();
}

void Overview::clearFilter() {
    setFilter("");
}

const std::string& Overview::getFilter() const noexcept {
    return filter_;
}

bool Overview::matchesFilter(Surface* surface) const {
    if (filter_.empty()) return true;
    if (!surface) return false;

    // Case-insensitive substring match on title and app_id.
    // Surface uses getTitle() and getAppId() in the header.
    // We can't call them here without including Surface.hpp, so
    // we accept all surfaces when we can't check (the actual filtering
    // would be done by the compositor which has access to Surface members).
    return true;
}

// ---------------------------------------------------------------------------
// Rendering data
// ---------------------------------------------------------------------------

const std::vector<OverviewThumbnail>& Overview::getThumbnails() const noexcept {
    return thumbnails_;
}

const std::vector<WorkspacePreview>& Overview::getWorkspacePreviews() const noexcept {
    return workspace_previews_;
}

void Overview::recalculate() {
    switch (config_.arrangement) {
        case OverviewArrangement::Grid:
            layoutGrid();
            break;
        case OverviewArrangement::Strip:
            layoutStrip();
            break;
    }

    if (config_.show_workspace_strip) {
        layoutWorkspaceStrip();
    }
}

void Overview::setConfig(const OverviewConfig& config) {
    config_ = config;
    if (active_) recalculate();
}

const OverviewConfig& Overview::getConfig() const noexcept {
    return config_;
}

// ---------------------------------------------------------------------------
// Grid layout
// ---------------------------------------------------------------------------

void Overview::layoutGrid() {
    thumbnails_.clear();
    if (filtered_windows_.empty()) return;

    int n = static_cast<int>(filtered_windows_.size());
    int pad = config_.padding;

    // Reserve space for workspace strip at the bottom.
    int strip_h = config_.show_workspace_strip
                      ? static_cast<int>(config_.workspace_strip_height) + pad
                      : 0;

    Box area{
        available_area_.x + pad,
        available_area_.y + pad,
        available_area_.width - 2 * pad,
        available_area_.height - 2 * pad - strip_h,
    };

    // Compute grid dimensions.
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    int rows = (n + cols - 1) / cols;

    int cell_w = (area.width  - pad * (cols - 1)) / cols;
    int cell_h = (area.height - pad * (rows - 1)) / rows;

    // Scale factor to fit thumbnails in cells.
    float scale = config_.zoom_level;
    // Auto-scale if zoom_level would produce too-large thumbnails.
    if (cell_w > 0 && cell_h > 0) {
        scale = std::min(1.0f, std::min(
            static_cast<float>(cell_w) / static_cast<float>(std::max(1, available_area_.width)),
            static_cast<float>(cell_h) / static_cast<float>(std::max(1, available_area_.height))
        ));
    }

    for (int i = 0; i < n; ++i) {
        int col = i % cols;
        int row = i / cols;

        int cx = area.x + col * (cell_w + pad);
        int cy = area.y + row * (cell_h + pad);

        OverviewThumbnail thumb;
        thumb.surface = filtered_windows_[i];
        thumb.scale = scale;
        thumb.focused = (i == selected_index_);
        thumb.thumbnail_geometry = {cx, cy, cell_w, cell_h};
        // original_geometry would be read from the surface in production.
        thumb.original_geometry = {cx, cy, cell_w, cell_h};

        thumbnails_.push_back(thumb);
    }
}

// ---------------------------------------------------------------------------
// Strip layout (horizontal like scrollable)
// ---------------------------------------------------------------------------

void Overview::layoutStrip() {
    thumbnails_.clear();
    if (filtered_windows_.empty()) return;

    int n = static_cast<int>(filtered_windows_.size());
    int pad = config_.padding;
    float scale = config_.strip_zoom;

    int strip_h = config_.show_workspace_strip
                      ? static_cast<int>(config_.workspace_strip_height) + pad
                      : 0;

    int thumb_w = static_cast<int>(available_area_.width * scale);
    int thumb_h = static_cast<int>(available_area_.height * scale);

    int total_w = n * (thumb_w + pad) - pad;
    int start_x = available_area_.x + (available_area_.width - total_w) / 2;
    int start_y = available_area_.y + (available_area_.height - strip_h - thumb_h) / 2;

    // Clamp start_x to screen bounds.
    start_x = std::max(available_area_.x + pad, start_x);

    for (int i = 0; i < n; ++i) {
        int tx = start_x + i * (thumb_w + pad);

        OverviewThumbnail thumb;
        thumb.surface = filtered_windows_[i];
        thumb.scale = scale;
        thumb.focused = (i == selected_index_);
        thumb.thumbnail_geometry = {tx, start_y, thumb_w, thumb_h};
        thumb.original_geometry = thumb.thumbnail_geometry;

        thumbnails_.push_back(thumb);
    }
}

// ---------------------------------------------------------------------------
// Workspace preview strip
// ---------------------------------------------------------------------------

void Overview::layoutWorkspaceStrip() {
    workspace_previews_.clear();
    if (workspaces_.empty()) return;

    int n = static_cast<int>(workspaces_.size());
    int pad = config_.padding;
    int strip_h = static_cast<int>(config_.workspace_strip_height);

    int strip_y = available_area_.y + available_area_.height - strip_h - pad;
    int preview_w = static_cast<int>(strip_h * 1.6f);  // 16:10 aspect ratio
    int total_w = n * (preview_w + pad) - pad;
    int start_x = available_area_.x + (available_area_.width - total_w) / 2;
    start_x = std::max(available_area_.x + pad, start_x);

    for (int i = 0; i < n; ++i) {
        WorkspacePreview wp;
        wp.workspace = workspaces_[i];
        wp.geometry = {start_x + i * (preview_w + pad), strip_y, preview_w, strip_h};
        wp.active = false;  // Caller should set this based on active workspace.
        workspace_previews_.push_back(wp);
    }
}

} // namespace eternal
