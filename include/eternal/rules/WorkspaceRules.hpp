#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace eternal {

// ---------------------------------------------------------------------------
// Per-workspace rule settings
// ---------------------------------------------------------------------------

struct WorkspaceRuleSettings {
    std::optional<std::string> monitor;
    std::optional<std::string> default_layout;
    std::optional<int>         gaps_in;
    std::optional<int>         gaps_out;
    std::optional<int>         border_size;
    std::optional<int>         rounding;
    std::optional<bool>        decorate;
    std::optional<bool>        shadow;
    std::optional<bool>        persistent;
    std::optional<std::string> on_created_empty;   // dispatcher command
};

// ---------------------------------------------------------------------------
// WorkspaceRules - per-workspace overrides keyed by workspace id
// ---------------------------------------------------------------------------

class WorkspaceRules {
public:
    WorkspaceRules() = default;

    /// Set or update the rule for workspace \p id.
    void setRule(int workspace_id, WorkspaceRuleSettings settings);

    /// Remove the rule for workspace \p id.
    void removeRule(int workspace_id);

    /// Clear all workspace rules.
    void clear();

    /// Get the resolved settings for \p workspace_id.
    /// Returns nullopt if no rule exists.
    [[nodiscard]] const WorkspaceRuleSettings* getRule(int workspace_id) const;

    /// Number of registered rules.
    [[nodiscard]] std::size_t size() const noexcept { return rules_.size(); }

    /// Direct access for serialisation / inspection.
    [[nodiscard]] const std::unordered_map<int, WorkspaceRuleSettings>& rules() const noexcept {
        return rules_;
    }

private:
    std::unordered_map<int, WorkspaceRuleSettings> rules_;
};

} // namespace eternal
