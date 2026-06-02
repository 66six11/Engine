#include "editor_viewport_overlay_provider.hpp"

namespace asharia::editor {

    EditorViewportWorldGridSettings defaultEditorSceneGridSettings() {
        return EditorViewportWorldGridSettings{
            .planeY = 0.0F,
            .minorSpacing = 1.0F,
            .majorSpacing = 10.0F,
            .fadeStart = 0.0F,
            .fadeEnd = 0.0F,
            .opacity = 1.0F,
        };
    }

    std::size_t EditorViewportOverlayPacketList::debugWorldLineCount() const {
        std::size_t count = 0;
        for (const EditorViewportOverlayPacket& packet : packets) {
            count += packet.debugWorldLines.size();
        }
        return count;
    }

    EditorViewportOverlayPacketList collectBuiltInEditorViewportOverlayPackets(
        const EditorViewportOverlayProviderContext& context) {
        static_cast<void>(context);
        EditorViewportOverlayPacketList packetList;
        return packetList;
    }

} // namespace asharia::editor
