#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "eternal/utils/Types.hpp"

namespace eternal {

// Forward declaration -- the actual surface type is defined elsewhere.
struct Surface;

enum class Direction : uint8_t {
    Left,
    Right,
    Up,
    Down,
};

enum class LayoutType : uint8_t {
    Scrollable,
    Dwindle,
    Master,
    Monocle,
    Floating,
    Grid,
    Spiral,
    Columns,
};

struct GapConfig {
    int inner = 5;
    int outer = 10;
};

// ---------------------------------------------------------------------------
// Abstract layout interface
// ---------------------------------------------------------------------------

class ILayout {
public:
    virtual ~ILayout() = default;

    /// Add a window to the layout.  The layout decides where to place it.
    virtual void addWindow(Surface* surface) = 0;

    /// Remove a window from the layout.
    virtual void removeWindow(Surface* surface) = 0;

    /// Move keyboard focus to the next window in layout order.
    virtual void focusNext() = 0;

    /// Move keyboard focus to the previous window in layout order.
    virtual void focusPrev() = 0;

    /// Move keyboard focus in the given spatial direction.
    virtual void focusDirection(Direction dir) = 0;

    /// Move the currently focused window in the given direction.
    virtual void moveWindow(Direction dir) = 0;

    /// Resize a specific window by the given delta.
    virtual void resizeWindow(Surface* surface, SizeDelta delta) = 0;

    /// Recalculate all window positions within the given available area.
    virtual void recalculate(Box availableArea) = 0;

    /// Return all windows managed by this layout (in layout order).
    [[nodiscard]] virtual std::vector<Surface*> getWindows() const = 0;

    /// Update inner/outer gap configuration and recalculate.
    virtual void setGaps(GapConfig gaps) = 0;

    /// Return the layout type enum.
    [[nodiscard]] virtual LayoutType getType() const noexcept = 0;

    /// Return a human-readable name for this layout.
    [[nodiscard]] virtual std::string_view getName() const noexcept = 0;
};

} // namespace eternal
