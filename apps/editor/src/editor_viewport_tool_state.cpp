#include "editor_viewport_tool_state.hpp"

#include <string>

namespace asharia::editor {
    namespace {

        struct StateMessageDesc {
            std::string_view label;
            std::string_view reason;
        };

        [[nodiscard]] std::string stateMessage(StateMessageDesc desc) {
            std::string message{desc.label};
            if (!desc.reason.empty()) {
                message += ": ";
                message += desc.reason;
            }
            return message;
        }

    } // namespace

    EditorViewportToolState::EditorViewportToolState(EditorEventQueue& eventQueue)
        : eventQueue_(&eventQueue) {}

    EditorViewportToolStateSnapshot EditorViewportToolState::snapshot() const {
        return EditorViewportToolStateSnapshot{
            .revision = revision_,
            .activeTool = activeTool_,
            .transformSpace = transformSpace_,
            .pivotMode = pivotMode_,
            .snapEnabled = snapEnabled_,
            .overlayFlags = overlayFlags_,
            .viewMode = viewMode_,
            .playPreviewState = playPreviewState_,
            .availability = availability_,
        };
    }

    bool EditorViewportToolState::canActivate(EditorViewportActiveTool tool) const {
        switch (tool) {
        case EditorViewportActiveTool::View:
            return true;
        case EditorViewportActiveTool::Select:
            return availability_.selectionToolAvailable;
        case EditorViewportActiveTool::Move:
        case EditorViewportActiveTool::Rotate:
        case EditorViewportActiveTool::Scale:
            return availability_.transformToolsAvailable;
        }
        return false;
    }

    bool EditorViewportToolState::setActiveTool(EditorViewportActiveTool tool,
                                                std::string_view reason) {
        if (!canActivate(tool)) {
            emitUnavailableTool(tool, reason);
            return false;
        }
        if (activeTool_ == tool) {
            return false;
        }

        activeTool_ = tool;
        ++revision_;
        emitChanged("activeTool", editorViewportActiveToolName(tool),
                    "Viewport active tool changed", reason);
        return true;
    }

    bool EditorViewportToolState::setTransformSpace(EditorViewportTransformSpace space,
                                                    std::string_view reason) {
        if (transformSpace_ == space) {
            return false;
        }

        transformSpace_ = space;
        ++revision_;
        emitChanged("transformSpace", editorViewportTransformSpaceName(space),
                    "Viewport transform space changed", reason);
        return true;
    }

    bool EditorViewportToolState::setPivotMode(EditorViewportPivotMode mode,
                                               std::string_view reason) {
        if (pivotMode_ == mode) {
            return false;
        }

        pivotMode_ = mode;
        ++revision_;
        emitChanged("pivotMode", editorViewportPivotModeName(mode), "Viewport pivot mode changed",
                    reason);
        return true;
    }

    bool EditorViewportToolState::setSnapEnabled(bool enabled, std::string_view reason) {
        if (snapEnabled_ == enabled) {
            return false;
        }

        snapEnabled_ = enabled;
        ++revision_;
        emitChanged("snap", enabled ? "Snap On" : "Snap Off", "Viewport snap state changed",
                    reason);
        return true;
    }

    bool EditorViewportToolState::setOverlayFlags(EditorViewportOverlayFlags flags,
                                                  std::string_view reason) {
        if (sameEditorViewportOverlayFlags(overlayFlags_, flags)) {
            return false;
        }

        overlayFlags_ = flags;
        ++revision_;
        emitChanged("overlayFlags", "Overlay Flags", "Viewport overlay flags changed", reason);
        return true;
    }

    bool EditorViewportToolState::setGridVisible(bool visible, std::string_view reason) {
        EditorViewportOverlayFlags flags = overlayFlags_;
        flags.gridVisible = visible;
        return setOverlayFlags(flags, reason);
    }

    bool EditorViewportToolState::setViewMode(EditorViewportViewMode mode,
                                              std::string_view reason) {
        if (viewMode_ == mode) {
            return false;
        }

        viewMode_ = mode;
        ++revision_;
        emitChanged("viewMode", editorViewportViewModeName(mode), "Viewport view mode changed",
                    reason);
        return true;
    }

    bool EditorViewportToolState::setPlayPreviewState(EditorViewportPlayPreviewState state,
                                                      std::string_view reason) {
        if (playPreviewState_ == state) {
            return false;
        }

        playPreviewState_ = state;
        ++revision_;
        emitChanged("playPreviewState", editorViewportPlayPreviewStateName(state),
                    "Viewport play/edit preview state changed", reason);
        return true;
    }

    void EditorViewportToolState::emitChanged(std::string_view subjectId, std::string_view label,
                                              std::string_view message, std::string_view reason,
                                              EditorEventOutcome outcome,
                                              EditorEventSeverity severity) {
        if (eventQueue_ == nullptr) {
            return;
        }

        eventQueue_->push(EditorEvent{
            .kind = EditorEventKind::ViewportToolStateChanged,
            .sourceId = EditorId{.value = "scene-view"},
            .metadata =
                EditorEventMetadata{
                    .revision = revision_,
                    .subjectId = std::string{subjectId},
                    .label = std::string{label},
                    .message = stateMessage(StateMessageDesc{.label = message, .reason = reason}),
                    .severity = severity,
                    .outcome = outcome,
                },
        });
    }

    void EditorViewportToolState::emitUnavailableTool(EditorViewportActiveTool tool,
                                                      std::string_view reason) {
        emitChanged("activeTool", editorViewportActiveToolName(tool),
                    "Viewport tool is pending provider support", reason, EditorEventOutcome::Noop,
                    EditorEventSeverity::Warning);
    }

    std::string_view editorViewportActiveToolName(EditorViewportActiveTool tool) noexcept {
        switch (tool) {
        case EditorViewportActiveTool::View:
            return "View";
        case EditorViewportActiveTool::Select:
            return "Select";
        case EditorViewportActiveTool::Move:
            return "Move";
        case EditorViewportActiveTool::Rotate:
            return "Rotate";
        case EditorViewportActiveTool::Scale:
            return "Scale";
        }
        return "Unknown";
    }

    std::string_view editorViewportTransformSpaceName(EditorViewportTransformSpace space) noexcept {
        switch (space) {
        case EditorViewportTransformSpace::Local:
            return "Local";
        case EditorViewportTransformSpace::World:
            return "World";
        }
        return "Unknown";
    }

    std::string_view editorViewportPivotModeName(EditorViewportPivotMode mode) noexcept {
        switch (mode) {
        case EditorViewportPivotMode::Pivot:
            return "Pivot";
        case EditorViewportPivotMode::Center:
            return "Center";
        }
        return "Unknown";
    }

    std::string_view editorViewportViewModeName(EditorViewportViewMode mode) noexcept {
        switch (mode) {
        case EditorViewportViewMode::Shaded:
            return "Shaded";
        case EditorViewportViewMode::Wireframe:
            return "Wireframe";
        }
        return "Unknown";
    }

    std::string_view
    editorViewportPlayPreviewStateName(EditorViewportPlayPreviewState state) noexcept {
        switch (state) {
        case EditorViewportPlayPreviewState::Edit:
            return "Edit";
        case EditorViewportPlayPreviewState::PlayPreview:
            return "Play Preview";
        }
        return "Unknown";
    }

} // namespace asharia::editor
