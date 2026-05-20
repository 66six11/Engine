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

    struct EditorViewportOverlayFlags {
        bool gridVisible{};
        bool gizmoVisible{};
        bool wireVisible{};
        bool selectionOutlineVisible{};
        bool debugOverlayVisible{};
        bool debugGizmoVisible{};
    };

    [[nodiscard]] constexpr EditorViewportOverlayFlags defaultEditorSceneViewOverlayFlags() {
        return EditorViewportOverlayFlags{
            .gridVisible = true,
            .gizmoVisible = true,
            .wireVisible = false,
            .selectionOutlineVisible = true,
            .debugOverlayVisible = false,
            .debugGizmoVisible = false,
        };
    }

    struct EditorViewportRequest {
        EditorId panelId;
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D extent;
        EditorViewportOverlayFlags overlayFlags;
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
        EditorViewportOverlayFlags overlayFlags;
    };

    [[nodiscard]] bool isRenderableEditorExtent(EditorExtent2D extent);
    [[nodiscard]] bool hasEditorViewportTexture(const EditorViewportTexture& texture);
    [[nodiscard]] bool anyEditorViewportOverlayFlagEnabled(EditorViewportOverlayFlags flags);
    [[nodiscard]] bool anyEditorSceneOnlyOverlayFlagEnabled(EditorViewportOverlayFlags flags);
    [[nodiscard]] EditorViewportOverlayFlags
    effectiveEditorViewportOverlayFlags(EditorViewportKind kind, EditorViewportOverlayFlags flags);

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
