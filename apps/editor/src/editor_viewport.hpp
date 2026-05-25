#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "editor_id.hpp"

namespace asharia::editor {

    struct EditorExtent2D {
        std::uint32_t width{1};
        std::uint32_t height{1};
    };

    // Viewport-local pixel coordinate: origin at the viewport top-left, x right, y down.
    struct EditorViewportPoint {
        float x{};
        float y{};
    };

    using EditorViewportMatrix4x4 = std::array<float, 16>;

    [[nodiscard]] constexpr EditorViewportMatrix4x4 editorViewportIdentityMatrix() {
        return EditorViewportMatrix4x4{
            1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        };
    }

    struct EditorViewportCamera {
        EditorViewportMatrix4x4 view{editorViewportIdentityMatrix()};
        EditorViewportMatrix4x4 projection{editorViewportIdentityMatrix()};
        EditorViewportMatrix4x4 viewProjection{editorViewportIdentityMatrix()};
        std::array<float, 3> position{};
        std::array<float, 3> target{};
        std::array<float, 3> up{0.0F, 1.0F, 0.0F};
        float verticalFovRadians{1.04719758F};
        float aspectRatio{1.0F};
        float nearPlane{0.1F};
        float farPlane{1000.0F};
    };

    struct EditorViewportWorldRay {
        // Picking origin on the camera near clipping plane.
        std::array<float, 3> origin{};
        std::array<float, 3> nearPoint{};
        std::array<float, 3> farPoint{};
        std::array<float, 3> direction{};
    };

    enum class EditorViewportKind {
        Scene,
        Game,
        Preview,
    };

    enum class EditorUiTextureColorSpace {
        LinearColor,
        SrgbColor,
        AlphaCoverage,
        Data,
    };

    enum class EditorViewportRefreshPolicy {
        OnDemand,
        Continuous,
    };

    enum class EditorViewportRepaintReason : std::uint32_t {
        None = 0,
        InitialTextureMissing = 1U << 0U,
        Resize = 1U << 1U,
        CameraInputChanged = 1U << 2U,
        OverlayFlagsChanged = 1U << 3U,
        SelectionOrGizmoDirty = 1U << 4U,
        AssetOrMaterialDirty = 1U << 5U,
        FrameDebugEventChanged = 1U << 6U,
        AlwaysRefresh = 1U << 7U,
    };

    using EditorViewportRepaintReasons = std::uint32_t;

    [[nodiscard]] constexpr EditorViewportRepaintReasons
    editorViewportRepaintReasonMask(EditorViewportRepaintReason reason) {
        return static_cast<EditorViewportRepaintReasons>(reason);
    }

    constexpr void addEditorViewportRepaintReason(EditorViewportRepaintReasons& reasons,
                                                  EditorViewportRepaintReason reason) {
        reasons |= editorViewportRepaintReasonMask(reason);
    }

    [[nodiscard]] constexpr bool
    hasEditorViewportRepaintReason(EditorViewportRepaintReasons reasons,
                                   EditorViewportRepaintReason reason) {
        return (reasons & editorViewportRepaintReasonMask(reason)) != 0U;
    }

    struct EditorViewportOverlayFlags {
        bool gridVisible{};
        bool gizmoVisible{};
        bool wireVisible{};
        bool selectionOutlineVisible{};
        bool debugOverlayVisible{};
        bool debugGizmoVisible{};
    };

    struct EditorViewportRefreshRequest {
        EditorViewportRefreshPolicy policy{EditorViewportRefreshPolicy::Continuous};
        EditorViewportRepaintReasons repaintReasons{};
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
        EditorViewportCamera camera;
        EditorViewportOverlayFlags overlayFlags;
        EditorViewportRefreshRequest refresh;
    };

    struct EditorViewportTexture {
        std::uintptr_t textureId{};
        EditorExtent2D extent;
        EditorUiTextureColorSpace colorSpace{EditorUiTextureColorSpace::LinearColor};
        std::uint64_t frameIndex{};
    };

    struct EditorViewportResult {
        EditorId panelId;
        EditorViewportKind kind{EditorViewportKind::Scene};
        EditorExtent2D requestedExtent;
        EditorViewportTexture texture;
        EditorViewportOverlayFlags overlayFlags;
    };

    [[nodiscard]] bool isRenderableEditorExtent(EditorExtent2D extent);
    [[nodiscard]] EditorViewportCamera defaultEditorSceneViewCamera(EditorExtent2D extent);
    [[nodiscard]] EditorViewportCamera
    editorViewportCameraForExtent(const EditorViewportCamera& camera, EditorExtent2D extent);
    void orbitEditorViewportCamera(EditorViewportCamera& camera, float deltaYaw, float deltaPitch);
    void panEditorViewportCamera(EditorViewportCamera& camera, float deltaX, float deltaY,
                                 EditorExtent2D extent);
    void dollyEditorViewportCamera(EditorViewportCamera& camera, float delta);
    [[nodiscard]] std::optional<EditorViewportWorldRay>
    unprojectEditorViewportPoint(const EditorViewportCamera& camera, EditorExtent2D extent,
                                 EditorViewportPoint point);
    [[nodiscard]] bool hasEditorViewportTexture(const EditorViewportTexture& texture);
    [[nodiscard]] bool anyEditorViewportOverlayFlagEnabled(EditorViewportOverlayFlags flags);
    [[nodiscard]] bool anyEditorSceneOnlyOverlayFlagEnabled(EditorViewportOverlayFlags flags);
    [[nodiscard]] bool sameEditorViewportOverlayFlags(EditorViewportOverlayFlags lhs,
                                                      EditorViewportOverlayFlags rhs);
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
