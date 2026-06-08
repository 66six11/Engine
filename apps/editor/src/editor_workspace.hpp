#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "editor_panel.hpp"

namespace asharia::editor {

    struct EditorWorkspaceDockPanel {
        std::string_view panelId;
        EditorDockSlot dockSlot{EditorDockSlot::Center};
    };

    struct EditorWorkspaceLayoutPreset {
        std::string_view id;
        std::string_view title;
        std::span<const EditorWorkspaceDockPanel> panels;
        float leftRatio{0.22F};
        float bottomRatio{0.24F};
        float rightRatio{0.32F};
        float rightBottomRatio{0.48F};
    };

    [[nodiscard]] const EditorWorkspaceLayoutPreset& defaultEditorWorkspaceLayoutPreset();

    class EditorWorkspaceController {
    public:
        [[nodiscard]] const EditorWorkspaceLayoutPreset& activePreset() const;
        void requestLayoutReset();
        [[nodiscard]] bool consumeLayoutResetRequest();
        void notifyLayoutApplied();
        [[nodiscard]] std::uint64_t layoutResetRequestCount() const;
        [[nodiscard]] std::uint64_t layoutApplyCount() const;

    private:
        std::string_view activePresetId_{"default"};
        bool layoutResetRequested_{};
        std::uint64_t layoutResetRequestCount_{};
        std::uint64_t layoutApplyCount_{};
    };

} // namespace asharia::editor
