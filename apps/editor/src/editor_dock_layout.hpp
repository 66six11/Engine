#pragma once

#include <imgui.h>

#include "editor_panel.hpp"
#include "editor_workspace.hpp"

namespace asharia::editor {

    class EditorI18n;

    struct EditorDockLayoutBuildDesc {
        const EditorPanelRegistry& panelRegistry;
        const EditorI18n& i18n;
        const ImGuiViewport& viewport;
        ImGuiID dockspaceId{};
        const EditorWorkspaceLayoutPreset& preset;
    };

    [[nodiscard]] bool editorDockLayoutExists(ImGuiID dockspaceId);
    void buildEditorDockLayout(const EditorDockLayoutBuildDesc& desc);

} // namespace asharia::editor
