#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "eternal/layout/ILayout.hpp"

namespace eternal {

struct Surface;
struct Output;

using WorkspaceID = int64_t;

class Workspace {
public:
    Workspace(WorkspaceID id, const std::string& name, Output* output);
    ~Workspace() = default;

    void addWindow(Surface* surface);
    void removeWindow(Surface* surface);

    [[nodiscard]] const std::vector<Surface*>& getWindows() const { return m_surfaces; }

    void activate();
    void deactivate();

    [[nodiscard]] bool isEmpty() const { return m_surfaces.empty(); }

    void setLayout(LayoutType type);
    [[nodiscard]] LayoutType getLayout() const {
        return m_layoutOverride.value_or(LayoutType::Dwindle);
    }

    [[nodiscard]] WorkspaceID getId() const { return m_id; }
    [[nodiscard]] const std::string& getName() const { return m_name; }
    [[nodiscard]] Output* getOutput() const { return m_output; }
    [[nodiscard]] bool isVisible() const { return m_isVisible; }
    [[nodiscard]] bool isSpecial() const { return m_isSpecial; }
    [[nodiscard]] bool isPersistent() const { return m_persistent; }
    [[nodiscard]] bool isNamed() const { return m_named; }
    [[nodiscard]] int getNumber() const { return m_number; }

    void setOutput(Output* output) { m_output = output; }
    void setName(const std::string& name) { m_name = name; }
    void setSpecial(bool special) { m_isSpecial = special; }
    void setPersistent(bool persistent) { m_persistent = persistent; }
    void setNamed(bool named) { m_named = named; }
    void setNumber(int number) { m_number = number; }
    void setGapOverride(std::optional<int> gap) { m_gapOverride = gap; }

    [[nodiscard]] std::optional<int> getGapOverride() const { return m_gapOverride; }

    /// Scroll offset for vertical workspace animation.
    [[nodiscard]] float getScrollOffset() const { return m_scrollOffset; }
    void setScrollOffset(float offset) { m_scrollOffset = offset; }

private:
    WorkspaceID m_id;
    std::string m_name;
    Output* m_output = nullptr;
    std::vector<Surface*> m_surfaces;
    bool m_isVisible = false;
    bool m_isSpecial = false;
    bool m_persistent = false;
    bool m_named = false;
    int m_number = 0;
    std::optional<LayoutType> m_layoutOverride;
    std::optional<int> m_gapOverride;
    float m_scrollOffset = 0.0f;
};

} // namespace eternal
