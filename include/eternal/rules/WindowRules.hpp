#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Match criteria for a window rule
// ---------------------------------------------------------------------------

struct WindowMatchCriteria {
    std::optional<std::string> window_class;
    std::optional<std::string> title;
    std::optional<bool>        is_floating;
    std::optional<bool>        is_fullscreen;
    std::optional<int>         workspace;
    std::optional<std::string> on_output;
    std::optional<bool>        is_pinned;
    std::optional<bool>        xwayland;
    std::optional<std::string> initial_class;
    std::optional<std::string> initial_title;
};

// ---------------------------------------------------------------------------
// Actions that a matched rule can apply
// ---------------------------------------------------------------------------

struct WindowRuleAction {
    std::optional<bool>        set_float;
    std::optional<bool>        set_tile;
    std::optional<bool>        set_fullscreen;
    std::optional<bool>        set_maximize;
    std::optional<bool>        set_pin;
    std::optional<float>       set_opacity;       // 0.0 - 1.0
    std::optional<int>         set_workspace;
    std::optional<std::string> set_monitor;
    std::optional<std::pair<int,int>> set_size;    // width, height
    std::optional<std::pair<int,int>> set_position;// x, y
    std::optional<bool>        noblur;
    std::optional<bool>        noshadow;
    std::optional<bool>        noborder;
    std::optional<bool>        norounding;
    std::optional<std::string> animation_style;
    std::optional<std::string> custom_shader;  ///< Per-window custom shader path
    std::optional<std::string> builtin_shader; ///< Built-in shader name (grayscale, sepia, invert, chromatic)
    std::optional<bool>        no_dim;         ///< Exclude from dim-inactive effect
    std::optional<std::string> group;
    std::optional<bool>        tab;
};

// ---------------------------------------------------------------------------
// A single window rule: criteria + action
// ---------------------------------------------------------------------------

struct WindowRule {
    WindowMatchCriteria criteria;
    WindowRuleAction    action;
};

// ---------------------------------------------------------------------------
// WindowRules - manages the ordered list of rules
// ---------------------------------------------------------------------------

class WindowRules {
public:
    WindowRules() = default;

    /// Add a new rule. Rules are evaluated in insertion order.
    void addRule(WindowRule rule);

    /// Remove all rules.
    void clear();

    /// Evaluate all rules against a window described by \p criteria and
    /// return the merged set of actions that should be applied.
    WindowRuleAction evaluate(const WindowMatchCriteria& window) const;

    /// Number of registered rules.
    [[nodiscard]] std::size_t size() const noexcept { return rules_.size(); }

    /// Direct access for serialisation / inspection.
    [[nodiscard]] const std::vector<WindowRule>& rules() const noexcept { return rules_; }

private:
    static bool matches(const WindowMatchCriteria& rule_criteria,
                        const WindowMatchCriteria& window);
    static void mergeAction(WindowRuleAction& dst, const WindowRuleAction& src);

    std::vector<WindowRule> rules_;
};

} // namespace eternal
