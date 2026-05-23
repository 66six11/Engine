#include "editor_viewport_overlay_provider.hpp"

#include <array>

namespace asharia::editor {

    namespace {

        [[nodiscard]] EditorViewportOverlayPacket
        makeSceneGridOverlayPacket(EditorViewportKind viewportKind) {
            constexpr int kGridHalfExtent = 5;
            constexpr auto kGridExtent = static_cast<float>(kGridHalfExtent);
            constexpr std::size_t kGridLineCount = 22U;
            constexpr std::array<float, 4> kMinorLineColor{0.36F, 0.39F, 0.44F, 0.62F};
            constexpr std::array<float, 4> kXAxisColor{0.84F, 0.22F, 0.18F, 1.0F};
            constexpr std::array<float, 4> kZAxisColor{0.22F, 0.42F, 0.92F, 1.0F};

            EditorViewportOverlayPacket packet{
                .overlayId = "scene.grid",
                .viewportKind = viewportKind,
                .debugWorldLines = {},
            };
            packet.debugWorldLines.reserve(kGridLineCount);
            for (int index = -kGridHalfExtent; index <= kGridHalfExtent; ++index) {
                const auto offset = static_cast<float>(index);
                packet.debugWorldLines.push_back(EditorDebugWorldLine{
                    .start = {-kGridExtent, 0.0F, offset},
                    .end = {kGridExtent, 0.0F, offset},
                    .color = index == 0 ? kXAxisColor : kMinorLineColor,
                });
                packet.debugWorldLines.push_back(EditorDebugWorldLine{
                    .start = {offset, 0.0F, -kGridExtent},
                    .end = {offset, 0.0F, kGridExtent},
                    .color = index == 0 ? kZAxisColor : kMinorLineColor,
                });
            }
            return packet;
        }

    } // namespace

    std::size_t EditorViewportOverlayPacketList::debugWorldLineCount() const {
        std::size_t count = 0;
        for (const EditorViewportOverlayPacket& packet : packets) {
            count += packet.debugWorldLines.size();
        }
        return count;
    }

    EditorViewportOverlayPacketList collectBuiltInEditorViewportOverlayPackets(
        const EditorViewportOverlayProviderContext& context) {
        EditorViewportOverlayPacketList packetList;
        if (context.viewportKind == EditorViewportKind::Scene && context.overlayFlags.gridVisible) {
            packetList.packets.push_back(makeSceneGridOverlayPacket(context.viewportKind));
        }
        return packetList;
    }

} // namespace asharia::editor
