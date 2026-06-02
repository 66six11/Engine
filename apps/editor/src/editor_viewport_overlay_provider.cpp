#include "editor_viewport_overlay_provider.hpp"

#include <algorithm>

namespace asharia::editor {

    namespace {

        constexpr std::array<EditorViewportOverlayProviderDesc, 5>
            kBuiltInEditorViewportOverlayProviders{
                EditorViewportOverlayProviderDesc{
                    .providerId = kEditorSceneGridOverlayProviderId,
                    .overlayId = kEditorSceneGridOverlayId,
                    .scope = EditorViewportOverlayProviderScope::SceneOnly,
                },
                EditorViewportOverlayProviderDesc{
                    .providerId = kEditorSceneTransformGizmoOverlayProviderId,
                    .overlayId = kEditorSceneTransformGizmoOverlayId,
                    .scope = EditorViewportOverlayProviderScope::SceneOnly,
                },
                EditorViewportOverlayProviderDesc{
                    .providerId = kEditorSceneSelectionOutlineOverlayProviderId,
                    .overlayId = kEditorSceneSelectionOutlineOverlayId,
                    .scope = EditorViewportOverlayProviderScope::SceneOnly,
                },
                EditorViewportOverlayProviderDesc{
                    .providerId = kEditorDebugOverlayProviderId,
                    .overlayId = kEditorDebugOverlayId,
                    .scope = EditorViewportOverlayProviderScope::SceneAndGame,
                },
                EditorViewportOverlayProviderDesc{
                    .providerId = kEditorDebugGizmoOverlayProviderId,
                    .overlayId = kEditorDebugGizmoOverlayId,
                    .scope = EditorViewportOverlayProviderScope::SceneAndGame,
                },
            };

        [[nodiscard]] bool overlayFlagEnabled(std::string_view overlayId,
                                              EditorViewportOverlayFlags flags) {
            if (overlayId == kEditorSceneGridOverlayId) {
                return flags.gridVisible;
            }
            if (overlayId == kEditorSceneTransformGizmoOverlayId) {
                return flags.gizmoVisible;
            }
            if (overlayId == kEditorSceneSelectionOutlineOverlayId) {
                return flags.selectionOutlineVisible;
            }
            if (overlayId == kEditorDebugOverlayId) {
                return flags.debugOverlayVisible;
            }
            if (overlayId == kEditorDebugGizmoOverlayId) {
                return flags.debugGizmoVisible;
            }
            return false;
        }

    } // namespace

    EditorViewportWorldGridSettings defaultEditorSceneGridSettings() {
        return EditorViewportWorldGridSettings{
            .planeY = 0.0F,
            .minorSpacing = 1.0F,
            .majorSpacing = 10.0F,
            .fadeStart = 0.0F,
            .fadeEnd = 0.0F,
            .opacity = 1.0F,
            .color = {0.36F, 0.39F, 0.44F, 1.0F},
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

    std::span<const EditorViewportOverlayProviderDesc> builtInEditorViewportOverlayProviders() {
        return kBuiltInEditorViewportOverlayProviders;
    }

    const EditorViewportOverlayProviderDesc*
    findBuiltInEditorViewportOverlayProvider(std::string_view providerId) {
        const auto providers = builtInEditorViewportOverlayProviders();
        const auto found = std::ranges::find_if(providers, [providerId](const auto& provider) {
            return provider.providerId == providerId;
        });
        if (found == providers.end()) {
            return nullptr;
        }
        return &(*found);
    }

    bool editorViewportOverlayProviderSupports(const EditorViewportOverlayProviderDesc& provider,
                                               EditorViewportKind viewportKind) {
        switch (provider.scope) {
        case EditorViewportOverlayProviderScope::SceneOnly:
            return viewportKind == EditorViewportKind::Scene;
        case EditorViewportOverlayProviderScope::SceneAndGame:
            return viewportKind == EditorViewportKind::Scene ||
                   viewportKind == EditorViewportKind::Game;
        }
        return false;
    }

    bool editorViewportOverlayProviderEnabled(const EditorViewportOverlayProviderDesc& provider,
                                              const EditorViewportOverlayProviderContext& context) {
        return editorViewportOverlayProviderSupports(provider, context.viewportKind) &&
               overlayFlagEnabled(provider.overlayId, context.overlayFlags);
    }

} // namespace asharia::editor
