#include "eternal/rules/WorkspaceRules.hpp"

namespace eternal {

void WorkspaceRules::setRule(int workspace_id, WorkspaceRuleSettings settings) {
    rules_[workspace_id] = std::move(settings);
}

void WorkspaceRules::removeRule(int workspace_id) {
    rules_.erase(workspace_id);
}

void WorkspaceRules::clear() {
    rules_.clear();
}

const WorkspaceRuleSettings* WorkspaceRules::getRule(int workspace_id) const {
    auto it = rules_.find(workspace_id);
    if (it == rules_.end()) return nullptr;
    return &it->second;
}

} // namespace eternal
