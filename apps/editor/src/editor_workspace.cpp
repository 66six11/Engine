#include "editor_workspace.hpp"

#include <array>

namespace asharia::editor {

    namespace {

        constexpr std::array<EditorWorkspaceDockPanel, 6> kDefaultWorkspacePanels{{
            EditorWorkspaceDockPanel{.panelId = "scene-view",
                                     .dockSlot = EditorDockSlot::Center},
            EditorWorkspaceDockPanel{.panelId = "render-graph",
                                     .dockSlot = EditorDockSlot::Center},
            EditorWorkspaceDockPanel{.panelId = "frame-debugger",
                                     .dockSlot = EditorDockSlot::RightTop},
            EditorWorkspaceDockPanel{.panelId = "editor-settings",
                                     .dockSlot = EditorDockSlot::RightTop},
            EditorWorkspaceDockPanel{.panelId = "ui-style-preview",
                                     .dockSlot = EditorDockSlot::RightBottom},
            EditorWorkspaceDockPanel{.panelId = "log", .dockSlot = EditorDockSlot::Bottom},
        }};

        const EditorWorkspaceLayoutPreset kDefaultWorkspace{
            .id = "default",
            .title = "Default",
            .panels = std::span<const EditorWorkspaceDockPanel>{kDefaultWorkspacePanels},
            .bottomRatio = 0.24F,
            .rightRatio = 0.32F,
            .rightBottomRatio = 0.48F,
        };

    } // namespace

    const EditorWorkspaceLayoutPreset& defaultEditorWorkspaceLayoutPreset() {
        return kDefaultWorkspace;
    }

    const EditorWorkspaceLayoutPreset& EditorWorkspaceController::activePreset() const {
        if (activePresetId_ == kDefaultWorkspace.id) {
            return kDefaultWorkspace;
        }
        return defaultEditorWorkspaceLayoutPreset();
    }

    void EditorWorkspaceController::requestLayoutReset() {
        layoutResetRequested_ = true;
        ++layoutResetRequestCount_;
    }

    bool EditorWorkspaceController::consumeLayoutResetRequest() {
        const bool requested = layoutResetRequested_;
        layoutResetRequested_ = false;
        return requested;
    }

    void EditorWorkspaceController::notifyLayoutApplied() {
        ++layoutApplyCount_;
    }

    std::uint64_t EditorWorkspaceController::layoutResetRequestCount() const {
        return layoutResetRequestCount_;
    }

    std::uint64_t EditorWorkspaceController::layoutApplyCount() const {
        return layoutApplyCount_;
    }

} // namespace asharia::editor
