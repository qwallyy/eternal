#include "eternal/layout/ColumnsLayout.hpp"

#include "eternal/core/Surface.hpp"

#include <algorithm>
#include <cmath>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ColumnsLayout::ColumnsLayout() : ColumnsLayout(ColumnsLayoutConfig{}) {}

ColumnsLayout::ColumnsLayout(ColumnsLayoutConfig config)
    : config_(config) {
    config_.column_count = std::max(1, config_.column_count);
    columns_.resize(config_.column_count);
}

ColumnsLayout::~ColumnsLayout() = default;

// ---------------------------------------------------------------------------
// ILayout identity
// ---------------------------------------------------------------------------

LayoutType       ColumnsLayout::getType() const noexcept { return LayoutType::Columns; }
std::string_view ColumnsLayout::getName() const noexcept { return "columns"; }

// ---------------------------------------------------------------------------
// Gaps
// ---------------------------------------------------------------------------

void ColumnsLayout::setGaps(GapConfig gaps) {
    gaps_ = gaps;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Window list
// ---------------------------------------------------------------------------

std::vector<Surface*> ColumnsLayout::getWindows() const {
    std::vector<Surface*> all;
    for (auto& col : columns_) {
        all.insert(all.end(), col.begin(), col.end());
    }
    return all;
}

// ---------------------------------------------------------------------------
// Helper: find column and row of a surface
// ---------------------------------------------------------------------------

namespace {
struct CellPos { int col = -1; int row = -1; };

CellPos findSurface(const std::vector<std::vector<Surface*>>& columns, Surface* s) {
    for (int c = 0; c < static_cast<int>(columns.size()); ++c) {
        for (int r = 0; r < static_cast<int>(columns[c].size()); ++r) {
            if (columns[c][r] == s) return {c, r};
        }
    }
    return {};
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Columns-specific
// ---------------------------------------------------------------------------

void ColumnsLayout::setColumnCount(int count) {
    count = std::max(1, count);
    config_.column_count = count;

    // Gather all windows and redistribute round-robin.
    auto all = getWindows();
    columns_.clear();
    columns_.resize(count);

    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        columns_[i % count].push_back(all[i]);
    }

    recalculate(available_area_);
}

int ColumnsLayout::getColumnCount() const noexcept {
    return config_.column_count;
}

void ColumnsLayout::setRatios(std::vector<float> ratios) {
    config_.ratios = std::move(ratios);
    config_.equal_width = false;
    recalculate(available_area_);
}

const std::vector<float>& ColumnsLayout::getRatios() const noexcept {
    return config_.ratios;
}

// ---------------------------------------------------------------------------
// addWindow -- round-robin assignment
// ---------------------------------------------------------------------------

void ColumnsLayout::addWindow(Surface* surface) {
    if (!surface) return;

    // Ensure we have enough column vectors.
    if (static_cast<int>(columns_.size()) < config_.column_count) {
        columns_.resize(config_.column_count);
    }

    // Find the column with fewest windows (round-robin effect).
    int minCol = 0;
    size_t minSize = columns_[0].size();
    for (int c = 1; c < static_cast<int>(columns_.size()); ++c) {
        if (columns_[c].size() < minSize) {
            minSize = columns_[c].size();
            minCol = c;
        }
    }

    columns_[minCol].push_back(surface);
    focused_column_ = minCol;
    focused_row_ = static_cast<int>(columns_[minCol].size()) - 1;

    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// removeWindow
// ---------------------------------------------------------------------------

void ColumnsLayout::removeWindow(Surface* surface) {
    if (!surface) return;

    auto pos = findSurface(columns_, surface);
    if (pos.col < 0) return;

    columns_[pos.col].erase(columns_[pos.col].begin() + pos.row);

    // Update focus.
    if (pos.col == focused_column_ && pos.row == focused_row_) {
        // Try to stay in the same column.
        if (!columns_[pos.col].empty()) {
            focused_row_ = std::min(pos.row,
                                    static_cast<int>(columns_[pos.col].size()) - 1);
        } else {
            // Find any non-empty column.
            focused_column_ = 0;
            focused_row_ = 0;
            for (int c = 0; c < static_cast<int>(columns_.size()); ++c) {
                if (!columns_[c].empty()) {
                    focused_column_ = c;
                    break;
                }
            }
        }
    }

    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void ColumnsLayout::focusNext() {
    auto all = getWindows();
    if (all.empty()) return;

    auto pos = findSurface(columns_, all.empty() ? nullptr
                           : (focused_column_ < static_cast<int>(columns_.size()) &&
                              focused_row_ < static_cast<int>(columns_[focused_column_].size()))
                                 ? columns_[focused_column_][focused_row_]
                                 : nullptr);

    // Flatten index, advance, un-flatten.
    int flat = 0;
    for (int c = 0; c < pos.col; ++c) flat += static_cast<int>(columns_[c].size());
    flat += pos.row;

    flat = (flat + 1) % static_cast<int>(all.size());

    // Convert flat back to col/row.
    int cur = 0;
    for (int c = 0; c < static_cast<int>(columns_.size()); ++c) {
        if (cur + static_cast<int>(columns_[c].size()) > flat) {
            focused_column_ = c;
            focused_row_ = flat - cur;
            return;
        }
        cur += static_cast<int>(columns_[c].size());
    }
}

void ColumnsLayout::focusPrev() {
    auto all = getWindows();
    if (all.empty()) return;

    int flat = 0;
    for (int c = 0; c < focused_column_; ++c) flat += static_cast<int>(columns_[c].size());
    flat += focused_row_;

    int n = static_cast<int>(all.size());
    flat = (flat - 1 + n) % n;

    int cur = 0;
    for (int c = 0; c < static_cast<int>(columns_.size()); ++c) {
        if (cur + static_cast<int>(columns_[c].size()) > flat) {
            focused_column_ = c;
            focused_row_ = flat - cur;
            return;
        }
        cur += static_cast<int>(columns_[c].size());
    }
}

void ColumnsLayout::focusDirection(Direction dir) {
    switch (dir) {
        case Direction::Left:
            if (focused_column_ > 0) {
                focused_column_--;
                auto& col = columns_[focused_column_];
                focused_row_ = col.empty() ? 0
                    : std::min(focused_row_, static_cast<int>(col.size()) - 1);
            }
            break;
        case Direction::Right:
            if (focused_column_ < static_cast<int>(columns_.size()) - 1) {
                focused_column_++;
                auto& col = columns_[focused_column_];
                focused_row_ = col.empty() ? 0
                    : std::min(focused_row_, static_cast<int>(col.size()) - 1);
            }
            break;
        case Direction::Up:
            if (focused_row_ > 0) focused_row_--;
            break;
        case Direction::Down:
            if (focused_column_ < static_cast<int>(columns_.size()) &&
                focused_row_ < static_cast<int>(columns_[focused_column_].size()) - 1) {
                focused_row_++;
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// moveWindow
// ---------------------------------------------------------------------------

void ColumnsLayout::moveWindow(Direction dir) {
    if (focused_column_ < 0 ||
        focused_column_ >= static_cast<int>(columns_.size()) ||
        columns_[focused_column_].empty()) return;

    Surface* s = columns_[focused_column_][focused_row_];

    switch (dir) {
        case Direction::Up:
            if (focused_row_ > 0) {
                std::swap(columns_[focused_column_][focused_row_],
                          columns_[focused_column_][focused_row_ - 1]);
                focused_row_--;
            }
            break;
        case Direction::Down:
            if (focused_row_ < static_cast<int>(columns_[focused_column_].size()) - 1) {
                std::swap(columns_[focused_column_][focused_row_],
                          columns_[focused_column_][focused_row_ + 1]);
                focused_row_++;
            }
            break;
        case Direction::Left:
            if (focused_column_ > 0) {
                columns_[focused_column_].erase(
                    columns_[focused_column_].begin() + focused_row_);
                focused_column_--;
                columns_[focused_column_].push_back(s);
                focused_row_ = static_cast<int>(columns_[focused_column_].size()) - 1;
            }
            break;
        case Direction::Right:
            if (focused_column_ < static_cast<int>(columns_.size()) - 1) {
                columns_[focused_column_].erase(
                    columns_[focused_column_].begin() + focused_row_);
                focused_column_++;
                columns_[focused_column_].push_back(s);
                focused_row_ = static_cast<int>(columns_[focused_column_].size()) - 1;
            }
            break;
    }

    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// resizeWindow -- no-op for fixed columns
// ---------------------------------------------------------------------------

void ColumnsLayout::resizeWindow(Surface* /*surface*/, SizeDelta /*delta*/) {
    // Fixed-columns layout does not support per-window resize.
}

// ---------------------------------------------------------------------------
// recalculate -- divide width by column count, stack windows within
// ---------------------------------------------------------------------------

void ColumnsLayout::recalculate(Box availableArea) {
    available_area_ = availableArea;

    auto all = getWindows();
    if (all.empty()) return;

    bool single = (all.size() == 1) && config_.no_gaps_when_only;
    int outer = single ? 0 : gaps_.outer;
    int inner = gaps_.inner;

    Box usable {
        availableArea.x + outer,
        availableArea.y + outer,
        availableArea.width  - 2 * outer,
        availableArea.height - 2 * outer,
    };
    if (usable.empty()) return;

    // Single window fills everything.
    if (all.size() == 1) {
        all[0]->setGeometry(usable.x, usable.y, usable.width, usable.height);
        return;
    }

    // Count active (non-empty) columns.
    int activeCols = 0;
    for (auto& col : columns_) {
        if (!col.empty()) ++activeCols;
    }
    if (activeCols == 0) return;

    int totalGapX = inner * (activeCols - 1);

    // Compute column widths.
    std::vector<int> colWidths;
    if (!config_.equal_width && static_cast<int>(config_.ratios.size()) >= activeCols) {
        // Use custom ratios.
        int remaining = usable.width - totalGapX;
        int ci = 0;
        for (auto& col : columns_) {
            if (col.empty()) continue;
            int w = static_cast<int>(remaining * config_.ratios[ci]);
            colWidths.push_back(std::max(1, w));
            ci++;
        }
    } else {
        int colW = (usable.width - totalGapX) / activeCols;
        colW = std::max(1, colW);
        for (auto& col : columns_) {
            if (!col.empty()) colWidths.push_back(colW);
        }
    }

    int curX = usable.x;
    int ci = 0;

    for (auto& col : columns_) {
        if (col.empty()) continue;

        int colW = colWidths[ci];
        int nWins = static_cast<int>(col.size());
        int totalGapY = inner * (nWins - 1);
        int winH = (usable.height - totalGapY) / nWins;
        winH = std::max(1, winH);

        for (int r = 0; r < nWins; ++r) {
            int cy = usable.y + r * (winH + inner);
            // Last window in column takes remaining height to avoid rounding gaps.
            int h = (r == nWins - 1)
                        ? (usable.y + usable.height - cy)
                        : winH;
            col[r]->setGeometry(curX, cy, colW, h);
        }

        curX += colW + inner;
        ci++;
    }
}

} // namespace eternal
