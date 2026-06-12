#pragma once

#include <cstdint>
#include <string_view>

#include "editor_event.hpp"
#include "editor_viewport.hpp"

namespace asharia::editor {

    enum class EditorViewportActiveTool {
        View,
        Select,
        Move,
        Rotate,
        Scale,
    };

    enum class EditorViewportTransformSpace {
        Local,
        World,
    };

    enum class EditorViewportPivotMode {
        Pivot,
        Center,
    };

    enum class EditorViewportViewMode {
        Shaded,
        Wireframe,
    };

    enum class EditorViewportPlayPreviewState {
        Edit,
        PlayPreview,
    };

    struct EditorViewportToolAvailability {
        bool selectionToolAvailable{};
        bool transformToolsAvailable{};
    };

    struct EditorViewportToolStateSnapshot {
        std::uint64_t revision{};
        EditorViewportActiveTool activeTool{EditorViewportActiveTool::View};
        EditorViewportTransformSpace transformSpace{EditorViewportTransformSpace::Local};
        EditorViewportPivotMode pivotMode{EditorViewportPivotMode::Pivot};
        bool snapEnabled{};
        EditorViewportOverlayFlags overlayFlags{defaultEditorSceneViewOverlayFlags()};
        EditorViewportViewMode viewMode{EditorViewportViewMode::Shaded};
        EditorViewportPlayPreviewState playPreviewState{EditorViewportPlayPreviewState::Edit};
        EditorViewportToolAvailability availability;
    };

    class EditorViewportToolState {
    public:
        explicit EditorViewportToolState(EditorEventQueue& eventQueue);

        [[nodiscard]] EditorViewportToolStateSnapshot snapshot() const;
        [[nodiscard]] bool canActivate(EditorViewportActiveTool tool) const;

        [[nodiscard]] bool setActiveTool(EditorViewportActiveTool tool,
                                         std::string_view reason = {});
        [[nodiscard]] bool setTransformSpace(EditorViewportTransformSpace space,
                                             std::string_view reason = {});
        [[nodiscard]] bool setPivotMode(EditorViewportPivotMode mode, std::string_view reason = {});
        [[nodiscard]] bool setSnapEnabled(bool enabled, std::string_view reason = {});
        [[nodiscard]] bool setOverlayFlags(EditorViewportOverlayFlags flags,
                                           std::string_view reason = {});
        [[nodiscard]] bool setGridVisible(bool visible, std::string_view reason = {});
        [[nodiscard]] bool setViewMode(EditorViewportViewMode mode, std::string_view reason = {});
        [[nodiscard]] bool setPlayPreviewState(EditorViewportPlayPreviewState state,
                                               std::string_view reason = {});

    private:
        void emitChanged(std::string_view subjectId, std::string_view label,
                         std::string_view message, std::string_view reason,
                         EditorEventOutcome outcome = EditorEventOutcome::Succeeded,
                         EditorEventSeverity severity = EditorEventSeverity::Info);
        void emitUnavailableTool(EditorViewportActiveTool tool, std::string_view reason);

        EditorEventQueue* eventQueue_{};
        std::uint64_t revision_{};
        EditorViewportActiveTool activeTool_{EditorViewportActiveTool::View};
        EditorViewportTransformSpace transformSpace_{EditorViewportTransformSpace::Local};
        EditorViewportPivotMode pivotMode_{EditorViewportPivotMode::Pivot};
        bool snapEnabled_{};
        EditorViewportOverlayFlags overlayFlags_{defaultEditorSceneViewOverlayFlags()};
        EditorViewportViewMode viewMode_{EditorViewportViewMode::Shaded};
        EditorViewportPlayPreviewState playPreviewState_{EditorViewportPlayPreviewState::Edit};
        EditorViewportToolAvailability availability_;
    };

    [[nodiscard]] std::string_view
    editorViewportActiveToolName(EditorViewportActiveTool tool) noexcept;
    [[nodiscard]] std::string_view
    editorViewportTransformSpaceName(EditorViewportTransformSpace space) noexcept;
    [[nodiscard]] std::string_view
    editorViewportPivotModeName(EditorViewportPivotMode mode) noexcept;
    [[nodiscard]] std::string_view editorViewportViewModeName(EditorViewportViewMode mode) noexcept;
    [[nodiscard]] std::string_view
    editorViewportPlayPreviewStateName(EditorViewportPlayPreviewState state) noexcept;

} // namespace asharia::editor
