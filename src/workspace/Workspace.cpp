#include <eternal/workspace/Workspace.hpp>

#include <algorithm>

namespace eternal {

Workspace::Workspace(WorkspaceID id, const std::string& name, Output* output)
    : m_id(id), m_name(name), m_output(output) {}

void Workspace::addWindow(Surface* surface) {
    if (surface)
        m_surfaces.push_back(surface);
}

void Workspace::removeWindow(Surface* surface) {
    std::erase(m_surfaces, surface);
}

void Workspace::activate() {
    m_isVisible = true;
}

void Workspace::deactivate() {
    m_isVisible = false;
}

void Workspace::setLayout(LayoutType type) {
    m_layoutOverride = type;
}

} // namespace eternal
