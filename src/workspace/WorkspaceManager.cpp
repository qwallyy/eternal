#include <eternal/workspace/WorkspaceManager.hpp>
#include "eternal/core/Server.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <cmath>

namespace eternal {

WorkspaceManager::WorkspaceManager(Server& server)
    : m_server(server) {}

WorkspaceManager::~WorkspaceManager() = default;

// ---------------------------------------------------------------------------
// WorkspaceSpring (Task 39)
// ---------------------------------------------------------------------------

void WorkspaceSpring::update(float dt) {
    float displacement = position - target;
    float springForce = -stiffness * displacement;
    float dampingForce = -damping * velocity;
    float acceleration = springForce + dampingForce;
    velocity += acceleration * dt;
    position += velocity * dt;
}

bool WorkspaceSpring::isSettled(float threshold) const {
    return std::abs(position - target) < threshold &&
           std::abs(velocity) < threshold;
}

// ---------------------------------------------------------------------------
// Workspace lifecycle
// ---------------------------------------------------------------------------

WorkspaceID WorkspaceManager::createWorkspace(const std::string& name, Output* output) {
    WorkspaceID id = m_nextId++;
    m_workspaces.push_back(std::make_unique<Workspace>(id, name, output));
    return id;
}

WorkspaceID WorkspaceManager::createNamedWorkspace(const std::string& name, Output* output) {
    // Check if already exists.
    if (auto* existing = findWorkspace(name)) {
        return existing->getId();
    }
    WorkspaceID id = createWorkspace(name, output);
    if (auto* ws = getWorkspace(id)) {
        ws->setPersistent(true);
    }
    return id;
}

void WorkspaceManager::deleteWorkspace(WorkspaceID id) {
    std::erase_if(m_workspaces, [id](const auto& ws) {
        return ws->getId() == id;
    });
}

void WorkspaceManager::switchTo(WorkspaceID id) {
    for (auto& ws : m_workspaces) {
        if (ws->getId() == id) {
            // Deactivate sibling workspaces on the same output
            Output* output = ws->getOutput();
            for (auto& other : m_workspaces) {
                if (other->getOutput() == output && other->getId() != id)
                    other->deactivate();
            }
            ws->activate();
            return;
        }
    }
}

void WorkspaceManager::switchToNext() {
    auto& compositor = m_server.getCompositor();
    Output* activeOutput = compositor.getActiveOutput();
    if (!activeOutput) return;

    auto workspaces = getWorkspaces(activeOutput);
    int idx = getActiveIndex(activeOutput);
    if (idx >= 0 && idx + 1 < static_cast<int>(workspaces.size())) {
        switchTo(workspaces[idx + 1]->getId());
    }
}

void WorkspaceManager::switchToPrev() {
    auto& compositor = m_server.getCompositor();
    Output* activeOutput = compositor.getActiveOutput();
    if (!activeOutput) return;

    auto workspaces = getWorkspaces(activeOutput);
    int idx = getActiveIndex(activeOutput);
    if (idx > 0) {
        switchTo(workspaces[idx - 1]->getId());
    }
}

void WorkspaceManager::switchToNumber(int number) {
    // 1-based index.
    int idx = 0;
    for (auto& ws : m_workspaces) {
        if (!ws->isSpecial()) {
            idx++;
            if (idx == number) {
                switchTo(ws->getId());
                return;
            }
        }
    }
}

void WorkspaceManager::switchToName(const std::string& name) {
    if (auto* ws = findWorkspace(name)) {
        switchTo(ws->getId());
    }
}

void WorkspaceManager::moveWindowToNext(Surface* surface) {
    auto* ws = findWorkspaceForSurface(surface);
    if (!ws) return;

    auto workspaces = getWorkspaces(ws->getOutput());
    for (size_t i = 0; i < workspaces.size(); ++i) {
        if (workspaces[i] == ws && i + 1 < workspaces.size()) {
            moveWindowTo(surface, workspaces[i + 1]->getId());
            return;
        }
    }
}

void WorkspaceManager::moveWindowToPrev(Surface* surface) {
    auto* ws = findWorkspaceForSurface(surface);
    if (!ws) return;

    auto workspaces = getWorkspaces(ws->getOutput());
    for (size_t i = 0; i < workspaces.size(); ++i) {
        if (workspaces[i] == ws && i > 0) {
            moveWindowTo(surface, workspaces[i - 1]->getId());
            return;
        }
    }
}

std::vector<Workspace*> WorkspaceManager::getAllWorkspaces() const {
    std::vector<Workspace*> result;
    result.reserve(m_workspaces.size());
    for (const auto& ws : m_workspaces) {
        result.push_back(ws.get());
    }
    return result;
}

void WorkspaceManager::moveWindowTo(Surface* surface, WorkspaceID wsId) {
    // Remove from current workspace
    for (auto& ws : m_workspaces)
        ws->removeWindow(surface);

    // Add to target
    if (auto* target = getWorkspace(wsId))
        target->addWindow(surface);
}

Workspace* WorkspaceManager::getActiveWorkspace(Output* output) const {
    for (const auto& ws : m_workspaces) {
        if (ws->getOutput() == output && ws->isVisible())
            return ws.get();
    }
    return nullptr;
}

std::vector<Workspace*> WorkspaceManager::getWorkspaces(Output* output) const {
    std::vector<Workspace*> result;
    for (const auto& ws : m_workspaces) {
        if (ws->getOutput() == output)
            result.push_back(ws.get());
    }
    return result;
}

Workspace* WorkspaceManager::getSpecialWorkspace() const {
    for (const auto& ws : m_workspaces) {
        if (ws->isSpecial())
            return ws.get();
    }
    return nullptr;
}

void WorkspaceManager::toggleSpecialWorkspace() {
    if (auto* special = getSpecialWorkspace()) {
        if (special->isVisible())
            special->deactivate();
        else
            special->activate();
    }
}

void WorkspaceManager::swapWorkspaces(WorkspaceID a, WorkspaceID b) {
    Workspace* wsA = getWorkspace(a);
    Workspace* wsB = getWorkspace(b);
    if (!wsA || !wsB)
        return;

    Output* outputA = wsA->getOutput();
    Output* outputB = wsB->getOutput();
    wsA->setOutput(outputB);
    wsB->setOutput(outputA);
}

void WorkspaceManager::moveWorkspaceToOutput(WorkspaceID wsId, Output* output) {
    if (auto* ws = getWorkspace(wsId))
        ws->setOutput(output);
}

std::size_t WorkspaceManager::getWorkspaceCount() const {
    return m_workspaces.size();
}

Workspace* WorkspaceManager::findWorkspace(const std::string& name) const {
    for (const auto& ws : m_workspaces) {
        if (ws->getName() == name)
            return ws.get();
    }
    return nullptr;
}

Workspace* WorkspaceManager::getWorkspace(WorkspaceID id) const {
    for (const auto& ws : m_workspaces) {
        if (ws->getId() == id)
            return ws.get();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Monitor assignment (Task 84)
// ---------------------------------------------------------------------------

void WorkspaceManager::addBinding(const WorkspaceBinding& binding) {
    m_bindings.push_back(binding);
    if (binding.workspaceId > 0) {
        m_boundOutput[binding.workspaceId] = binding.outputName;
    }
    LOG_DEBUG("WorkspaceManager: added binding ws='{}' -> output='{}'",
              binding.workspaceName, binding.outputName);
}

void WorkspaceManager::applyBindings() {
    auto& compositor = m_server.getCompositor();

    for (const auto& binding : m_bindings) {
        // Find the output by name.
        Output* target = nullptr;
        for (auto& outPtr : compositor.getOutputs()) {
            if (outPtr->getName() == binding.outputName) {
                target = outPtr.get();
                break;
            }
        }

        if (!target) {
            target = findFallbackOutput();
            if (target) {
                LOG_WARN("WorkspaceManager: bound output '{}' not found, "
                         "falling back to '{}'",
                         binding.outputName, target->getName());
            }
        }

        if (!target) continue;

        // Find or create the workspace.
        Workspace* ws = nullptr;
        if (binding.workspaceId > 0) {
            ws = getWorkspace(binding.workspaceId);
        }
        if (!ws && !binding.workspaceName.empty()) {
            ws = findWorkspace(binding.workspaceName);
        }

        if (!ws) {
            // Create it.
            std::string name = binding.workspaceName.empty()
                ? std::to_string(binding.workspaceId)
                : binding.workspaceName;
            WorkspaceID id = createWorkspace(name, target);
            ws = getWorkspace(id);
            if (ws && binding.persistent) {
                ws->setPersistent(true);
            }
        } else {
            ws->setOutput(target);
        }

        if (ws) {
            target->addWorkspace(ws);
            LOG_INFO("WorkspaceManager: bound workspace '{}' to output '{}'",
                     ws->getName(), target->getName());
        }
    }
}

void WorkspaceManager::bindToOutput(WorkspaceID wsId,
                                     const std::string& outputName) {
    m_boundOutput[wsId] = outputName;

    auto& compositor = m_server.getCompositor();
    Output* target = nullptr;
    for (auto& outPtr : compositor.getOutputs()) {
        if (outPtr->getName() == outputName) {
            target = outPtr.get();
            break;
        }
    }

    if (target) {
        if (auto* ws = getWorkspace(wsId)) {
            ws->setOutput(target);
            target->addWorkspace(ws);
        }
    } else {
        LOG_WARN("WorkspaceManager: output '{}' not available for binding",
                 outputName);
    }
}

void WorkspaceManager::moveWorkspaceToOutputByName(WorkspaceID wsId,
                                                    const std::string& outputName) {
    auto& compositor = m_server.getCompositor();
    for (auto& outPtr : compositor.getOutputs()) {
        if (outPtr->getName() == outputName) {
            moveWorkspaceToOutput(wsId, outPtr.get());
            // Update binding.
            m_boundOutput[wsId] = outputName;
            LOG_INFO("WorkspaceManager: moved workspace {} to output '{}'",
                     wsId, outputName);
            return;
        }
    }
    LOG_WARN("WorkspaceManager: output '{}' not found for move", outputName);
}

void WorkspaceManager::handleOutputDisconnected(Output* output) {
    if (!output) return;

    Output* fallback = findFallbackOutput(output);
    if (!fallback) {
        LOG_WARN("WorkspaceManager: no fallback output available");
        return;
    }

    // Move all workspaces from the disconnected output to the fallback.
    for (auto& ws : m_workspaces) {
        if (ws->getOutput() == output) {
            ws->setOutput(fallback);
            fallback->addWorkspace(ws.get());
            LOG_INFO("WorkspaceManager: moved workspace '{}' from '{}' to '{}'",
                     ws->getName(), output->getName(), fallback->getName());
        }
    }
}

void WorkspaceManager::handleOutputConnected(Output* output) {
    if (!output) return;

    // Restore any workspaces that were bound to this output.
    for (const auto& [wsId, outName] : m_boundOutput) {
        if (outName == output->getName()) {
            if (auto* ws = getWorkspace(wsId)) {
                Output* oldOutput = ws->getOutput();
                ws->setOutput(output);
                output->addWorkspace(ws);
                if (oldOutput && oldOutput != output) {
                    oldOutput->removeWorkspace(ws);
                }
                LOG_INFO("WorkspaceManager: restored workspace '{}' to '{}'",
                         ws->getName(), output->getName());
            }
        }
    }
}

Output* WorkspaceManager::findFallbackOutput(Output* exclude) const {
    auto& compositor = m_server.getCompositor();
    for (auto& outPtr : compositor.getOutputs()) {
        if (outPtr.get() != exclude) {
            return outPtr.get();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Dynamic workspace management (Task 39)
// ---------------------------------------------------------------------------

void WorkspaceManager::ensureEmptyTrailing(Output* output) {
    if (!output) return;

    auto workspaces = getWorkspaces(output);
    bool hasEmpty = false;
    if (!workspaces.empty() && workspaces.back()->isEmpty()) {
        hasEmpty = true;
    }

    if (!hasEmpty) {
        std::string name = std::to_string(m_nextId);
        createWorkspace(name, output);
    }
}

void WorkspaceManager::pruneEmptyWorkspaces(Output* output) {
    if (!output) return;

    auto workspaces = getWorkspaces(output);
    if (workspaces.size() <= 1) return;

    // Keep the last empty one as trailing.
    for (size_t i = 0; i + 1 < workspaces.size(); ++i) {
        auto* ws = workspaces[i];
        if (ws->isEmpty() && !ws->isPersistent() && !ws->isSpecial()) {
            deleteWorkspace(ws->getId());
        }
    }
}

void WorkspaceManager::renumberWorkspaces(Output* output) {
    if (!output) return;

    int num = 1;
    for (auto& ws : m_workspaces) {
        if (ws->getOutput() == output && !ws->isSpecial()) {
            ws->setName(std::to_string(num++));
        }
    }
}

// ---------------------------------------------------------------------------
// Animation (Task 39)
// ---------------------------------------------------------------------------

bool WorkspaceManager::tickAnimation(float dt) {
    bool anyActive = false;
    for (auto& state : m_animStates) {
        if (!state.spring.isSettled()) {
            state.spring.update(dt);
            anyActive = true;
        }
    }
    return anyActive;
}

bool WorkspaceManager::isAnimating() const noexcept {
    for (const auto& state : m_animStates) {
        if (!state.spring.isSettled()) return true;
    }
    return false;
}

float WorkspaceManager::getScrollOffset(Output* output) const {
    const auto* state = getAnimState(output);
    return state ? state->spring.position : 0.0f;
}

const WorkspaceSpring* WorkspaceManager::getSpring(Output* output) const {
    const auto* state = getAnimState(output);
    return state ? &state->spring : nullptr;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

Workspace* WorkspaceManager::findWorkspaceForSurface(Surface* surface) const {
    for (const auto& ws : m_workspaces) {
        for (auto* s : ws->getWindows()) {
            if (s == surface) return ws.get();
        }
    }
    return nullptr;
}

int WorkspaceManager::getActiveIndex(Output* output) const {
    auto workspaces = getWorkspaces(output);
    for (int i = 0; i < static_cast<int>(workspaces.size()); ++i) {
        if (workspaces[i]->isVisible()) return i;
    }
    return -1;
}

WorkspaceManager::OutputAnimState* WorkspaceManager::getAnimState(Output* output) {
    for (auto& state : m_animStates) {
        if (state.output == output) return &state;
    }
    return nullptr;
}

const WorkspaceManager::OutputAnimState* WorkspaceManager::getAnimState(Output* output) const {
    for (const auto& state : m_animStates) {
        if (state.output == output) return &state;
    }
    return nullptr;
}

void WorkspaceManager::ensureAnimState(Output* output) {
    if (!getAnimState(output)) {
        m_animStates.push_back({output, {}});
    }
}

} // namespace eternal
