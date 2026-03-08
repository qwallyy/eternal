#include "eternal/rules/WindowRules.hpp"

#include <regex>

namespace eternal {

// ---------------------------------------------------------------------------
// WindowRules
// ---------------------------------------------------------------------------

void WindowRules::addRule(WindowRule rule) {
    rules_.push_back(std::move(rule));
}

void WindowRules::clear() {
    rules_.clear();
}

WindowRuleAction WindowRules::evaluate(const WindowMatchCriteria& window) const {
    WindowRuleAction merged{};
    for (const auto& rule : rules_) {
        if (matches(rule.criteria, window)) {
            mergeAction(merged, rule.action);
        }
    }
    return merged;
}

bool WindowRules::matches(const WindowMatchCriteria& c,
                          const WindowMatchCriteria& w) {
    // Helper: match an optional regex pattern against an optional string value.
    auto matchOpt = [](const std::optional<std::string>& pattern,
                       const std::optional<std::string>& value) -> bool {
        if (!pattern.has_value()) return true;          // criterion not set
        if (!value.has_value()) return false;           // required but absent
        try {
            std::regex re(*pattern, std::regex::ECMAScript);
            return std::regex_search(*value, re);
        } catch (...) {
            return *pattern == *value;                  // fallback: literal
        }
    };

    auto matchBool = [](const std::optional<bool>& pattern,
                        const std::optional<bool>& value) -> bool {
        if (!pattern.has_value()) return true;
        if (!value.has_value()) return false;
        return *pattern == *value;
    };

    auto matchInt = [](const std::optional<int>& pattern,
                       const std::optional<int>& value) -> bool {
        if (!pattern.has_value()) return true;
        if (!value.has_value()) return false;
        return *pattern == *value;
    };

    if (!matchOpt(c.window_class, w.window_class))   return false;
    if (!matchOpt(c.title, w.title))                  return false;
    if (!matchBool(c.is_floating, w.is_floating))     return false;
    if (!matchBool(c.is_fullscreen, w.is_fullscreen)) return false;
    if (!matchInt(c.workspace, w.workspace))           return false;
    if (!matchOpt(c.on_output, w.on_output))           return false;
    if (!matchBool(c.is_pinned, w.is_pinned))         return false;
    if (!matchBool(c.xwayland, w.xwayland))           return false;
    if (!matchOpt(c.initial_class, w.initial_class))   return false;
    if (!matchOpt(c.initial_title, w.initial_title))   return false;

    return true;
}

void WindowRules::mergeAction(WindowRuleAction& dst, const WindowRuleAction& src) {
    // Later rules override earlier ones for each field.
    if (src.set_float)       dst.set_float       = src.set_float;
    if (src.set_tile)        dst.set_tile         = src.set_tile;
    if (src.set_fullscreen)  dst.set_fullscreen   = src.set_fullscreen;
    if (src.set_maximize)    dst.set_maximize     = src.set_maximize;
    if (src.set_pin)         dst.set_pin          = src.set_pin;
    if (src.set_opacity)     dst.set_opacity      = src.set_opacity;
    if (src.set_workspace)   dst.set_workspace    = src.set_workspace;
    if (src.set_monitor)     dst.set_monitor      = src.set_monitor;
    if (src.set_size)        dst.set_size         = src.set_size;
    if (src.set_position)    dst.set_position     = src.set_position;
    if (src.noblur)          dst.noblur           = src.noblur;
    if (src.noshadow)        dst.noshadow         = src.noshadow;
    if (src.noborder)        dst.noborder         = src.noborder;
    if (src.norounding)      dst.norounding       = src.norounding;
    if (src.animation_style) dst.animation_style  = src.animation_style;
    if (src.group)           dst.group            = src.group;
    if (src.tab)             dst.tab              = src.tab;
}

} // namespace eternal
