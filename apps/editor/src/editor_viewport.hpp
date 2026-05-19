#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "editor_id.hpp"

namespace asharia::editor {

    struct EditorExtent2D {
        std::uint32_t width{1};
        std::uint32_t height{1};
    };

    enum class EditorViewportKind {
        Scene,
        Game,
        Preview,
    };

    struct EditorViewportRequest {
        EditorId panelId;
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D extent;
    };

    struct EditorViewportTexture {
        std::uintptr_t textureId{};
        EditorExtent2D extent;
    };

    struct EditorViewportResult {
        EditorId panelId;
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        EditorViewportTexture texture;
    };

    [[nodiscard]] bool isRenderableEditorExtent(EditorExtent2D extent);
    [[nodiscard]] bool hasEditorViewportTexture(const EditorViewportTexture& texture);

    class EditorViewportPanelHost {
    public:
        EditorViewportPanelHost() = default;
        EditorViewportPanelHost(const EditorViewportPanelHost&) = delete;
        EditorViewportPanelHost& operator=(const EditorViewportPanelHost&) = delete;
        EditorViewportPanelHost(EditorViewportPanelHost&&) = delete;
        EditorViewportPanelHost& operator=(EditorViewportPanelHost&&) = delete;
        virtual ~EditorViewportPanelHost() = default;

        virtual void requestViewport(EditorViewportRequest request) = 0;
        [[nodiscard]] virtual std::optional<EditorViewportResult>
        acquireViewportTextureForDraw(std::string_view panelId) = 0;
    };

} // namespace asharia::editor
