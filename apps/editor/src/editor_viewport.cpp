#include "editor_viewport.hpp"

namespace asharia::editor {

    bool isRenderableEditorExtent(EditorExtent2D extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool hasEditorViewportTexture(const EditorViewportTexture& texture) {
        return texture.textureId != 0 && isRenderableEditorExtent(texture.extent);
    }

} // namespace asharia::editor
