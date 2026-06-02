#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "editor_viewport.hpp"

namespace asharia::editor {

    inline constexpr std::string_view kEditorSceneGridOverlayId = "scene.grid";
    inline constexpr std::string_view kEditorSceneTransformGizmoOverlayId = "scene.transform-gizmo";
    inline constexpr std::string_view kEditorSceneSelectionOutlineOverlayId =
        "scene.selection-outline";
    inline constexpr std::string_view kEditorDebugOverlayId = "debug.overlay";
    inline constexpr std::string_view kEditorDebugGizmoOverlayId = "debug.gizmo";
    inline constexpr std::string_view kEditorSceneGridOverlayProviderId = "provider.scene-grid";
    inline constexpr std::string_view kEditorSceneTransformGizmoOverlayProviderId =
        "provider.scene-transform-gizmo";
    inline constexpr std::string_view kEditorSceneSelectionOutlineOverlayProviderId =
        "provider.scene-selection-outline";
    inline constexpr std::string_view kEditorDebugOverlayProviderId = "provider.debug-overlay";
    inline constexpr std::string_view kEditorDebugGizmoOverlayProviderId = "provider.debug-gizmo";

    [[nodiscard]] EditorViewportWorldGridSettings defaultEditorSceneGridSettings();

    struct EditorDebugWorldLine {
        std::array<float, 3> start{};
        std::array<float, 3> end{};
        std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    };

    struct EditorViewportOverlayProviderContext {
        EditorViewportKind viewportKind{EditorViewportKind::Scene};
        EditorViewportCamera camera;
        EditorViewportOverlayFlags overlayFlags;
    };

    enum class EditorViewportOverlayProviderScope {
        SceneOnly,
        SceneAndGame,
    };

    struct EditorViewportOverlayProviderDesc {
        std::string_view providerId;
        std::string_view overlayId;
        EditorViewportOverlayProviderScope scope{EditorViewportOverlayProviderScope::SceneOnly};
    };

    struct EditorViewportOverlayPacket {
        std::string overlayId;
        EditorViewportKind viewportKind{EditorViewportKind::Scene};
        std::vector<EditorDebugWorldLine> debugWorldLines;
    };

    struct EditorViewportOverlayPacketList {
        std::vector<EditorViewportOverlayPacket> packets;

        [[nodiscard]] std::size_t debugWorldLineCount() const;
    };

    [[nodiscard]] EditorViewportOverlayPacketList
    collectBuiltInEditorViewportOverlayPackets(const EditorViewportOverlayProviderContext& context);
    [[nodiscard]] std::span<const EditorViewportOverlayProviderDesc>
    builtInEditorViewportOverlayProviders();
    [[nodiscard]] const EditorViewportOverlayProviderDesc*
    findBuiltInEditorViewportOverlayProvider(std::string_view providerId);
    [[nodiscard]] bool
    editorViewportOverlayProviderSupports(const EditorViewportOverlayProviderDesc& provider,
                                          EditorViewportKind viewportKind);
    [[nodiscard]] bool
    editorViewportOverlayProviderEnabled(const EditorViewportOverlayProviderDesc& provider,
                                         const EditorViewportOverlayProviderContext& context);

} // namespace asharia::editor
