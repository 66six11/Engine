#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "editor_viewport.hpp"

namespace asharia::editor {

    inline constexpr std::string_view kEditorSceneGridOverlayId = "scene.grid";
    inline constexpr std::string_view kEditorSceneTransformGizmoOverlayId =
        "scene.transform-gizmo";
    inline constexpr std::string_view kEditorSceneSelectionOutlineOverlayId =
        "scene.selection-outline";
    inline constexpr std::string_view kEditorDebugOverlayId = "debug.overlay";
    inline constexpr std::string_view kEditorDebugGizmoOverlayId = "debug.gizmo";

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

} // namespace asharia::editor
