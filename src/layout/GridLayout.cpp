#include "eternal/layout/GridLayout.hpp"

#include "eternal/core/Surface.hpp"

#include <algorithm>
#include <cmath>
#include <climits>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GridLayout::GridLayout() : GridLayout(GridLayoutConfig{}) {}

GridLayout::GridLayout(GridLayoutConfig config)
    : config_(config) {}

GridLayout::~GridLayout() = default;

// ---------------------------------------------------------------------------
// ILayout identity
// ---------------------------------------------------------------------------

LayoutType       GridLayout::getType() const noexcept { return LayoutType::Grid; }
std::string_view GridLayout::getName() const noexcept { return "grid"; }

// ---------------------------------------------------------------------------
// Gaps
// ---------------------------------------------------------------------------

void GridLayout::setGaps(GapConfig gaps) {
    gaps_ = gaps;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Window list
// ---------------------------------------------------------------------------

std::vector<Surface*> GridLayout::getWindows() const {
    return windows_;
}

// ---------------------------------------------------------------------------
// Grid dimension helpers
// ---------------------------------------------------------------------------

void GridLayout::computeGridDimensions() {
    int n = static_cast<int>(windows_.size());
    if (n <= 0) {
        computed_cols_ = 0;
        computed_rows_ = 0;
        return;
    }

    if (config_.columns > 0) {
        computed_cols_ = config_.columns;
    } else {
        computed_cols_ = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    }

    computed_rows_ = static_cast<int>(
        std::ceil(static_cast<double>(n) / computed_cols_));
}

void GridLayout::setColumns(int columns) {
    config_.columns = std::max(0, columns);
    computeGridDimensions();
    recalculate(available_area_);
}

int GridLayout::getColumns() const noexcept { return computed_cols_; }
int GridLayout::getRows()    const noexcept { return computed_rows_; }

// ---------------------------------------------------------------------------
// addWindow
// ---------------------------------------------------------------------------

void GridLayout::addWindow(Surface* surface) {
    if (!surface) return;
    windows_.push_back(surface);
    focused_index_ = static_cast<int>(windows_.size()) - 1;
    computeGridDimensions();
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// removeWindow
// ---------------------------------------------------------------------------

void GridLayout::removeWindow(Surface* surface) {
    if (!surface) return;
    auto it = std::find(windows_.begin(), windows_.end(), surface);
    if (it == windows_.end()) return;

    int idx = static_cast<int>(it - windows_.begin());
    windows_.erase(it);

    if (windows_.empty()) {
        focused_index_ = 0;
    } else {
        focused_index_ = std::min(idx, static_cast<int>(windows_.size()) - 1);
    }

    computeGridDimensions();
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void GridLayout::focusNext() {
    if (windows_.empty()) return;
    focused_index_ = (focused_index_ + 1) % static_cast<int>(windows_.size());
}

void GridLayout::focusPrev() {
    if (windows_.empty()) return;
    int n = static_cast<int>(windows_.size());
    focused_index_ = (focused_index_ - 1 + n) % n;
}

void GridLayout::focusDirection(Direction dir) {
    if (windows_.empty()) return;

    int n = static_cast<int>(windows_.size());
    computeGridDimensions();

    int row = focused_index_ / computed_cols_;
    int col = focused_index_ % computed_cols_;

    switch (dir) {
        case Direction::Left:
            if (col > 0) col--;
            break;
        case Direction::Right:
            if (col < computed_cols_ - 1 && (row * computed_cols_ + col + 1) < n)
                col++;
            break;
        case Direction::Up:
            if (row > 0) row--;
            break;
        case Direction::Down:
            if (row < computed_rows_ - 1) row++;
            break;
    }

    int newIdx = row * computed_cols_ + col;
    if (newIdx >= 0 && newIdx < n) {
        focused_index_ = newIdx;
    }
}

// ---------------------------------------------------------------------------
// moveWindow -- swap with neighbour in direction
// ---------------------------------------------------------------------------

void GridLayout::moveWindow(Direction dir) {
    if (windows_.size() < 2) return;

    int n = static_cast<int>(windows_.size());
    computeGridDimensions();

    int row = focused_index_ / computed_cols_;
    int col = focused_index_ % computed_cols_;
    int targetRow = row, targetCol = col;

    switch (dir) {
        case Direction::Left:  targetCol--; break;
        case Direction::Right: targetCol++; break;
        case Direction::Up:    targetRow--; break;
        case Direction::Down:  targetRow++; break;
    }

    int targetIdx = targetRow * computed_cols_ + targetCol;
    if (targetRow >= 0 && targetRow < computed_rows_ &&
        targetCol >= 0 && targetCol < computed_cols_ &&
        targetIdx >= 0 && targetIdx < n) {
        std::swap(windows_[focused_index_], windows_[targetIdx]);
        focused_index_ = targetIdx;
        recalculate(available_area_);
    }
}

// ---------------------------------------------------------------------------
// resizeWindow -- no-op for uniform grid
// ---------------------------------------------------------------------------

void GridLayout::resizeWindow(Surface* /*surface*/, SizeDelta /*delta*/) {
    // Grid cells are uniform; individual resize is not supported.
}

// ---------------------------------------------------------------------------
// recalculate -- divide area into grid cells
// ---------------------------------------------------------------------------

void GridLayout::recalculate(Box availableArea) {
    available_area_ = availableArea;
    int n = static_cast<int>(windows_.size());
    if (n == 0) return;

    computeGridDimensions();

    int outer = gaps_.outer;
    int inner = gaps_.inner;

    Box usable {
        availableArea.x + outer,
        availableArea.y + outer,
        availableArea.width  - 2 * outer,
        availableArea.height - 2 * outer,
    };
    if (usable.empty()) return;

    // Single window fills everything.
    if (n == 1) {
        windows_[0]->setGeometry(usable.x, usable.y, usable.width, usable.height);
        return;
    }

    int cols = computed_cols_;
    int rows = computed_rows_;

    int totalGapX = inner * (cols - 1);
    int totalGapY = inner * (rows - 1);
    int cellW = (usable.width  - totalGapX) / cols;
    int cellH = (usable.height - totalGapY) / rows;

    cellW = std::max(1, cellW);
    cellH = std::max(1, cellH);

    for (int i = 0; i < n; ++i) {
        int row = i / cols;
        int col = i % cols;

        int cx, cy;
        cy = usable.y + row * (cellH + inner);

        // Center the last row if it has fewer windows than columns.
        int windowsInLastRow = n - (rows - 1) * cols;
        if (row == rows - 1 && windowsInLastRow < cols) {
            int lastRowWidth = windowsInLastRow * cellW + (windowsInLastRow - 1) * inner;
            int offsetX = (usable.width - lastRowWidth) / 2;
            cx = usable.x + offsetX + col * (cellW + inner);
        } else {
            cx = usable.x + col * (cellW + inner);
        }

        windows_[i]->setGeometry(cx, cy, cellW, cellH);
    }
}

} // namespace eternal
