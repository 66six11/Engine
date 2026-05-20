#include "editor_viewport.hpp"

namespace asharia::editor {

    bool isRenderableEditorExtent(EditorExtent2D extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool hasEditorViewportTexture(const EditorViewportTexture& texture) {
        return texture.textureId != 0 && isRenderableEditorExtent(texture.extent);
    }

    bool anyEditorViewportOverlayFlagEnabled(EditorViewportOverlayFlags flags) {
        return flags.gridVisible || flags.gizmoVisible || flags.wireVisible ||
               flags.selectionOutlineVisible || flags.debugOverlayVisible ||
               flags.debugGizmoVisible;
    }

    bool anyEditorSceneOnlyOverlayFlagEnabled(EditorViewportOverlayFlags flags) {
        return flags.gridVisible || flags.gizmoVisible || flags.wireVisible ||
               flags.selectionOutlineVisible;
    }

    EditorViewportOverlayFlags
    effectiveEditorViewportOverlayFlags(EditorViewportKind kind, EditorViewportOverlayFlags flags) {
        if (kind == EditorViewportKind::Scene) {
            return flags;
        }
        if (kind == EditorViewportKind::Game) {
            return EditorViewportOverlayFlags{
                .debugOverlayVisible = flags.debugOverlayVisible,
                .debugGizmoVisible = flags.debugGizmoVisible,
            };
        }
        if (kind == EditorViewportKind::Preview) {
            return {};
        }
        return {};
    }

} // namespace asharia::editor
