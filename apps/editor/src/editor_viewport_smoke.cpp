#include "editor_viewport_smoke.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/core/log.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/render_view.hpp"

#include "editor_smoke.hpp"
#include "editor_viewport.hpp"
#include "editor_viewport_coordinator.hpp"
#include "editor_viewport_overlay_provider.hpp"
#include "imgui_texture_registry.hpp"

namespace asharia::editor {

    namespace {

        bool closeFloat(float lhs, float rhs) {
            return std::fabs(lhs - rhs) < 0.0001F;
        }

        float editorVec3Distance(std::array<float, 3> lhs, std::array<float, 3> rhs) {
            const float deltaX = lhs[0] - rhs[0];
            const float deltaY = lhs[1] - rhs[1];
            const float deltaZ = lhs[2] - rhs[2];
            return std::sqrt((deltaX * deltaX) + (deltaY * deltaY) + (deltaZ * deltaZ));
        }

        bool sameViewportOverlayFlags(EditorViewportOverlayFlags lhs,
                                      EditorViewportOverlayFlags rhs) {
            return lhs.gridVisible == rhs.gridVisible && lhs.gizmoVisible == rhs.gizmoVisible &&
                   lhs.wireVisible == rhs.wireVisible &&
                   lhs.selectionOutlineVisible == rhs.selectionOutlineVisible &&
                   lhs.debugOverlayVisible == rhs.debugOverlayVisible &&
                   lhs.debugGizmoVisible == rhs.debugGizmoVisible;
        }

        [[nodiscard]] std::uint64_t
        renderGraphPassTypeCount(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                 std::string_view passType) {
            return static_cast<std::uint64_t>(std::ranges::count_if(
                snapshot.passes, [passType](const asharia::RenderGraphDiagnosticsPassNode& pass) {
                    return pass.type == passType;
                }));
        }

        [[nodiscard]] std::uint64_t
        renderGraphOverlayCommandCount(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
            std::uint64_t commandCount = 0;
            for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
                if (pass.type == asharia::kBasicRenderViewOverlayPassType) {
                    commandCount += static_cast<std::uint64_t>(pass.commandCount);
                }
            }
            return commandCount;
        }

        [[nodiscard]] std::uint64_t
        renderGraphWorldGridCommandCount(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
            std::uint64_t commandCount = 0;
            for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
                if (pass.type == asharia::kBasicRenderViewWorldGridPassType) {
                    commandCount += static_cast<std::uint64_t>(pass.commandCount);
                }
            }
            return commandCount;
        }

        [[nodiscard]] bool
        hasSourceOverlayId(const asharia::BasicRenderViewOverlayDiagnostics& overlay,
                           std::string_view sourceOverlayId) {
            return std::ranges::find(overlay.sourceOverlayIds, sourceOverlayId) !=
                   overlay.sourceOverlayIds.end();
        }

        [[nodiscard]] bool
        matchesWorldGridSettings(const asharia::BasicRenderViewOverlayDiagnostics& overlay,
                                 EditorViewportWorldGridSettings expected) {
            const asharia::BasicRenderViewWorldGridDesc& worldGrid = overlay.worldGrid;
            return overlay.worldGridEnabled && worldGrid.enabled &&
                   closeFloat(worldGrid.planeY, expected.planeY) &&
                   closeFloat(worldGrid.minorSpacing, expected.minorSpacing) &&
                   closeFloat(worldGrid.majorSpacing, expected.majorSpacing) &&
                   closeFloat(worldGrid.fadeStart, expected.fadeStart) &&
                   closeFloat(worldGrid.fadeEnd, expected.fadeEnd) &&
                   closeFloat(worldGrid.opacity, expected.opacity);
        }

        [[nodiscard]] bool
        validateRenderViewCameraSmoke(const asharia::BasicRenderViewDiagnostics& diagnostics) {
            if (!closeFloat(diagnostics.camera.position[0], 0.0F) ||
                !closeFloat(diagnostics.camera.position[1], 2.0F) ||
                !closeFloat(diagnostics.camera.position[2], -6.0F) ||
                !closeFloat(diagnostics.camera.nearPlane, 0.1F) ||
                !closeFloat(diagnostics.camera.farPlane, 1000.0F) ||
                diagnostics.camera.projection[0] <= 0.0F ||
                diagnostics.camera.projection[5] <= 1.0F ||
                diagnostics.camera.viewProjection[10] == 0.0F) {
                asharia::logError(
                    "Editor viewport smoke recorded invalid RenderView camera diagnostics.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateEditorViewportCameraSmoke() {
            const EditorExtent2D extent{.width = 320, .height = 240};
            const EditorViewportCamera camera = defaultEditorSceneViewCamera(extent);
            const std::optional<EditorViewportWorldRay> ray = unprojectEditorViewportPoint(
                camera, extent, EditorViewportPoint{.x = 160.0F, .y = 120.0F});
            if (!ray || !closeFloat(ray->origin[0], ray->nearPoint[0]) ||
                !closeFloat(ray->origin[1], ray->nearPoint[1]) ||
                !closeFloat(ray->origin[2], ray->nearPoint[2]) ||
                !closeFloat(ray->nearPoint[0], 0.0F) ||
                !closeFloat(ray->nearPoint[1], 1.96837723F) ||
                !closeFloat(ray->nearPoint[2], -5.90513182F) ||
                !closeFloat(editorVec3Distance(camera.position, ray->nearPoint),
                            camera.nearPlane) ||
                !closeFloat(ray->direction[0], 0.0F) ||
                !closeFloat(ray->direction[1], -0.31622776F) ||
                !closeFloat(ray->direction[2], 0.94868332F)) {
                asharia::logError(
                    "Editor viewport smoke calculated an invalid center unproject ray.");
                return false;
            }

            const std::optional<EditorViewportWorldRay> topLeftRay = unprojectEditorViewportPoint(
                camera, extent, EditorViewportPoint{.x = 0.0F, .y = 0.0F});
            const std::optional<EditorViewportWorldRay> bottomRightRay =
                unprojectEditorViewportPoint(camera, extent,
                                             EditorViewportPoint{
                                                 .x = static_cast<float>(extent.width),
                                                 .y = static_cast<float>(extent.height),
                                             });
            if (!topLeftRay || !bottomRightRay || topLeftRay->direction[0] >= 0.0F ||
                topLeftRay->direction[1] <= 0.0F || bottomRightRay->direction[0] <= 0.0F ||
                bottomRightRay->direction[1] >= 0.0F) {
                asharia::logError(
                    "Editor viewport smoke found invalid viewport point orientation.");
                return false;
            }

            const float rayLength = std::sqrt((ray->direction[0] * ray->direction[0]) +
                                              (ray->direction[1] * ray->direction[1]) +
                                              (ray->direction[2] * ray->direction[2]));
            const EditorViewportCamera resizedCamera =
                editorViewportCameraForExtent(camera, EditorExtent2D{.width = 640, .height = 320});
            const std::optional<EditorViewportWorldRay> resizedTopLeftRay =
                unprojectEditorViewportPoint(resizedCamera,
                                             EditorExtent2D{.width = 640, .height = 320},
                                             EditorViewportPoint{.x = 0.0F, .y = 0.0F});
            EditorViewportCamera invalidCamera = camera;
            invalidCamera.viewProjection = {};
            if (!closeFloat(rayLength, 1.0F) || !closeFloat(resizedCamera.aspectRatio, 2.0F) ||
                !closeFloat(resizedCamera.position[0], camera.position[0]) ||
                !closeFloat(resizedCamera.position[1], camera.position[1]) ||
                !closeFloat(resizedCamera.position[2], camera.position[2]) ||
                resizedCamera.projection[0] >= resizedCamera.projection[5] || !resizedTopLeftRay ||
                std::fabs(resizedTopLeftRay->direction[0]) <= std::fabs(topLeftRay->direction[0]) ||
                unprojectEditorViewportPoint(camera, EditorExtent2D{.width = 0, .height = 240},
                                             EditorViewportPoint{}) != std::nullopt ||
                unprojectEditorViewportPoint(invalidCamera, extent, EditorViewportPoint{}) !=
                    std::nullopt) {
                asharia::logError("Editor viewport smoke found invalid camera extent handling.");
                return false;
            }

            EditorViewportCamera dollyCamera = camera;
            dollyEditorViewportCamera(dollyCamera, 1.0F);
            if (editorVec3Distance(dollyCamera.position, dollyCamera.target) >=
                editorVec3Distance(camera.position, camera.target)) {
                asharia::logError("Editor viewport smoke found invalid camera dolly direction.");
                return false;
            }

            EditorViewportCamera orbitCamera = camera;
            orbitEditorViewportCamera(orbitCamera, 0.0F, 0.1F);
            if (orbitCamera.position[1] <= camera.position[1] ||
                !closeFloat(editorVec3Distance(orbitCamera.position, orbitCamera.target),
                            editorVec3Distance(camera.position, camera.target))) {
                asharia::logError(
                    "Editor viewport smoke found invalid inverted camera orbit pitch direction.");
                return false;
            }

            EditorViewportCamera panCamera = camera;
            panEditorViewportCamera(panCamera, 10.0F, 10.0F, extent);
            if (panCamera.position[0] >= camera.position[0] ||
                panCamera.target[0] >= camera.target[0] ||
                panCamera.position[1] <= camera.position[1] ||
                panCamera.target[1] <= camera.target[1] ||
                !closeFloat(editorVec3Distance(panCamera.position, panCamera.target),
                            editorVec3Distance(camera.position, camera.target))) {
                asharia::logError(
                    "Editor viewport smoke found invalid inverted camera pan vertical direction.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        validateIdleSceneViewReuseSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult,
                                        const EditorViewportCoordinatorStats& viewportStats) {
            if (mode != EditorRunMode::SmokeViewport) {
                return true;
            }
            if (viewportStats.idleSceneViewFramesSkipped == 0) {
                asharia::logError("Editor viewport smoke did not skip idle Scene View recording.");
                return false;
            }
            if (viewportStats.sceneViewDiagnosticsFramesRecorded >=
                static_cast<std::uint64_t>(runResult.renderedFrames)) {
                asharia::logError(
                    "Editor viewport smoke did not reuse the idle Scene View texture.");
                return false;
            }
            if (viewportStats.repaintReasonFramesRecorded == 0) {
                asharia::logError(
                    "Editor viewport smoke did not record a repaint-reason RenderView.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        validateSceneRenderViewDiagnosticsSmoke(const EditorViewportCoordinator& viewportHost,
                                                asharia::BasicRenderViewDiagnostics& scene) {
            const std::optional<EditorRecordedRenderViewDiagnostics> sceneDiagnostics =
                viewportHost.latestRecordedRenderViewDiagnosticsForView("scene-view",
                                                                        EditorViewportKind::Scene);
            if (!sceneDiagnostics) {
                asharia::logError("Editor viewport smoke missed a keyed RenderView diagnostics "
                                  "snapshot.");
                return false;
            }

            scene = sceneDiagnostics->diagnostics;
            if (scene.renderGraph.passes.size() != 3U || scene.renderGraph.resources.size() != 2U ||
                scene.renderGraph.accessEdges.size() != 4U ||
                scene.renderGraph.dependencyEdges.size() != 2U ||
                scene.renderGraph.transitions.size() != 4U || scene.executionEvents.empty()) {
                asharia::logError(
                    "Editor viewport smoke recorded unexpected render view diagnostics counts: "
                    "passes " +
                    std::to_string(scene.renderGraph.passes.size()) + ", resources " +
                    std::to_string(scene.renderGraph.resources.size()) + ", access edges " +
                    std::to_string(scene.renderGraph.accessEdges.size()) + ", dependency edges " +
                    std::to_string(scene.renderGraph.dependencyEdges.size()) + ", transitions " +
                    std::to_string(scene.renderGraph.transitions.size()) + ", execution events " +
                    std::to_string(scene.executionEvents.size()) + ".");
                return false;
            }
            if (renderGraphPassTypeCount(scene.renderGraph,
                                         asharia::kBasicRenderViewWorldGridPassType) != 1U ||
                renderGraphWorldGridCommandCount(scene.renderGraph) != 6U ||
                renderGraphPassTypeCount(scene.renderGraph,
                                         asharia::kBasicRenderViewOverlayPassType) != 0U ||
                renderGraphOverlayCommandCount(scene.renderGraph) != 0U) {
                asharia::logError(
                    "Editor viewport smoke recorded invalid RenderView grid/overlay passes.");
                return false;
            }
            if (scene.viewKind != asharia::BasicRenderViewKind::Scene ||
                scene.frameParams.frameIndex == 0 || !scene.overlay.enabled ||
                !matchesWorldGridSettings(scene.overlay, EditorViewportWorldGridSettings{}) ||
                scene.overlay.debugWorldLineCount != 0 ||
                scene.overlay.sourceOverlayIds.size() != 3U ||
                !hasSourceOverlayId(scene.overlay, kEditorSceneGridOverlayId) ||
                !hasSourceOverlayId(scene.overlay, kEditorSceneTransformGizmoOverlayId) ||
                !hasSourceOverlayId(scene.overlay, kEditorSceneSelectionOutlineOverlayId)) {
                asharia::logError(
                    "Editor viewport smoke recorded invalid RenderView overlay prerequisites.");
                return false;
            }
            const auto worldGridDraw = std::ranges::find_if(
                scene.executionEvents, [](const asharia::BasicRenderViewExecutionEvent& event) {
                    return event.kind ==
                               asharia::BasicRenderViewExecutionEventKind::DrawFullscreenTriangle &&
                           event.label == "DrawWorldGrid";
                });
            if (worldGridDraw == scene.executionEvents.end() ||
                worldGridDraw->draw.vertexCount != 3U) {
                asharia::logError(
                    "Editor viewport smoke did not record a world-grid overlay draw event.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateSyntheticMultiViewDiagnosticsSmoke(
            EditorRunMode mode, const EditorViewportCoordinator& viewportHost,
            const EditorViewportCoordinatorStats& viewportStats) {
            if (mode != EditorRunMode::SmokeViewport) {
                return true;
            }
            if (viewportStats.multiViewFramesRecorded == 0 ||
                viewportStats.viewportRequestsRecorded < 4 ||
                viewportStats.gameViewDiagnosticsFramesRecorded == 0 ||
                viewportStats.previewViewDiagnosticsFramesRecorded == 0) {
                asharia::logError(
                    "Editor viewport smoke did not record multiple keyed RenderViews in one "
                    "frame.");
                return false;
            }

            const std::optional<EditorRecordedRenderViewDiagnostics> gameDiagnostics =
                viewportHost.latestRecordedRenderViewDiagnosticsForView("editor-smoke-game-view",
                                                                        EditorViewportKind::Game);
            const std::optional<EditorRecordedRenderViewDiagnostics> previewDiagnostics =
                viewportHost.latestRecordedRenderViewDiagnosticsForView(
                    "editor-smoke-preview-view", EditorViewportKind::Preview);
            const std::optional<EditorRecordedRenderViewDiagnostics> customSceneDiagnostics =
                viewportHost.latestRecordedRenderViewDiagnosticsForView(
                    "editor-smoke-custom-grid-scene-view", EditorViewportKind::Scene);
            if (!gameDiagnostics || !previewDiagnostics || !customSceneDiagnostics) {
                asharia::logError(
                    "Editor viewport smoke missed a keyed multi-view diagnostics snapshot.");
                return false;
            }

            const EditorViewportWorldGridSettings customSceneGrid{
                .planeY = 0.5F,
                .minorSpacing = 2.0F,
                .majorSpacing = 20.0F,
                .fadeStart = 8.0F,
                .fadeEnd = 80.0F,
                .opacity = 0.65F,
            };
            const asharia::BasicRenderViewDiagnostics& customScene =
                customSceneDiagnostics->diagnostics;
            if (customScene.viewKind != asharia::BasicRenderViewKind::Scene ||
                !customScene.overlay.enabled ||
                !matchesWorldGridSettings(customScene.overlay, customSceneGrid) ||
                customScene.overlay.debugWorldLineCount != 0 ||
                customScene.overlay.sourceOverlayIds.size() != 1U ||
                !hasSourceOverlayId(customScene.overlay, kEditorSceneGridOverlayId) ||
                renderGraphPassTypeCount(customScene.renderGraph,
                                         asharia::kBasicRenderViewWorldGridPassType) != 1U ||
                renderGraphWorldGridCommandCount(customScene.renderGraph) != 6U ||
                renderGraphPassTypeCount(customScene.renderGraph,
                                         asharia::kBasicRenderViewOverlayPassType) != 0U) {
                asharia::logError(
                    "Editor viewport smoke did not route custom Scene View grid settings.");
                return false;
            }

            const asharia::BasicRenderViewDiagnostics& game = gameDiagnostics->diagnostics;
            if (game.viewKind != asharia::BasicRenderViewKind::Game || !game.overlay.enabled ||
                game.overlay.worldGridEnabled || game.overlay.debugWorldLineCount != 0 ||
                game.overlay.sourceOverlayIds.size() != 2U ||
                !hasSourceOverlayId(game.overlay, kEditorDebugOverlayId) ||
                !hasSourceOverlayId(game.overlay, kEditorDebugGizmoOverlayId) ||
                renderGraphPassTypeCount(game.renderGraph,
                                         asharia::kBasicRenderViewWorldGridPassType) != 0U ||
                renderGraphPassTypeCount(game.renderGraph,
                                         asharia::kBasicRenderViewOverlayPassType) != 0U) {
                asharia::logError("Editor viewport smoke recorded invalid Game View diagnostics.");
                return false;
            }

            const asharia::BasicRenderViewDiagnostics& preview = previewDiagnostics->diagnostics;
            if (preview.viewKind != asharia::BasicRenderViewKind::Preview ||
                preview.overlay.enabled || !preview.overlay.sourceOverlayIds.empty() ||
                renderGraphPassTypeCount(preview.renderGraph,
                                         asharia::kBasicRenderViewWorldGridPassType) != 0U ||
                renderGraphPassTypeCount(preview.renderGraph,
                                         asharia::kBasicRenderViewOverlayPassType) != 0U) {
                asharia::logError("Editor viewport smoke leaked overlay inputs into Preview View.");
                return false;
            }
            return true;
        }

    } // namespace

    bool validateViewportSmokePresentation(EditorRunMode mode,
                                           const EditorSmokeRunResult& runResult,
                                           const EditorViewportCoordinator& viewportHost,
                                           const ImGuiTextureRegistryStats& textureRegistryStats) {
        if (!isEditorViewportSmokeMode(mode)) {
            return true;
        }
        if (!viewportHost.hasPresentedViewportTexture()) {
            asharia::logError("Editor viewport smoke did not present a sampled viewport texture.");
            return false;
        }
        if (viewportHost.textureFramesSubmitted() + 1U <
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor viewport smoke dropped sampled texture presentation during "
                              "resize.");
            return false;
        }
        if (textureRegistryStats.peakLiveDescriptors > kEditorViewportTextureDescriptorBudget) {
            asharia::logError("Editor viewport smoke exceeded ImGui texture descriptor budget.");
            return false;
        }
        return true;
    }

    bool validateViewportFlagsSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult,
                                    const EditorViewportCoordinator& viewportHost,
                                    const EditorViewportCoordinatorStats& viewportStats) {
        if (!isEditorViewportSmokeMode(mode)) {
            return true;
        }

        const EditorViewportOverlayFlags defaults = defaultEditorSceneViewOverlayFlags();
        if (!defaults.gridVisible || !defaults.gizmoVisible || defaults.wireVisible ||
            !defaults.selectionOutlineVisible || defaults.debugOverlayVisible ||
            defaults.debugGizmoVisible) {
            asharia::logError("Editor viewport smoke found invalid default overlay flags.");
            return false;
        }

        const EditorViewportOverlayFlags allFlags{
            .gridVisible = true,
            .gizmoVisible = true,
            .wireVisible = true,
            .selectionOutlineVisible = true,
            .debugOverlayVisible = true,
            .debugGizmoVisible = true,
        };
        if (!sameViewportOverlayFlags(
                effectiveEditorViewportOverlayFlags(EditorViewportKind::Scene, allFlags),
                allFlags)) {
            asharia::logError("Editor viewport smoke dropped Scene View overlay flags.");
            return false;
        }
        const EditorViewportOverlayFlags gameFlags =
            effectiveEditorViewportOverlayFlags(EditorViewportKind::Game, allFlags);
        if (gameFlags.gridVisible || gameFlags.gizmoVisible || gameFlags.wireVisible ||
            gameFlags.selectionOutlineVisible || !gameFlags.debugOverlayVisible ||
            !gameFlags.debugGizmoVisible) {
            asharia::logError(
                "Editor viewport smoke filtered Game View overlay flags incorrectly.");
            return false;
        }
        if (anyEditorViewportOverlayFlagEnabled(
                effectiveEditorViewportOverlayFlags(EditorViewportKind::Preview, allFlags))) {
            asharia::logError("Editor viewport smoke leaked overlay flags into Preview views.");
            return false;
        }
        const EditorViewportOverlayPacketList scenePackets =
            collectBuiltInEditorViewportOverlayPackets(EditorViewportOverlayProviderContext{
                .viewportKind = EditorViewportKind::Scene,
                .camera = defaultEditorSceneViewCamera(EditorExtent2D{.width = 320, .height = 240}),
                .overlayFlags = defaults,
            });
        const EditorViewportOverlayPacketList gamePackets =
            collectBuiltInEditorViewportOverlayPackets(EditorViewportOverlayProviderContext{
                .viewportKind = EditorViewportKind::Game,
                .camera = defaultEditorSceneViewCamera(EditorExtent2D{.width = 320, .height = 240}),
                .overlayFlags = gameFlags,
            });
        if (!scenePackets.packets.empty() || scenePackets.debugWorldLineCount() != 0 ||
            !gamePackets.packets.empty()) {
            asharia::logError("Editor viewport smoke detected invalid overlay provider packets.");
            return false;
        }
        if (viewportStats.overlayFlagFramesRendered == 0) {
            asharia::logError("Editor viewport smoke did not render a Scene View flagged frame.");
            return false;
        }
        if (viewportStats.overlayFlagTextureFramesAcquired == 0) {
            asharia::logError(
                "Editor viewport smoke did not acquire a Scene View flagged texture.");
            return false;
        }
        if (mode == EditorRunMode::SmokeViewport) {
            if (viewportStats.sceneViewOnlyFlagRequestsDiscarded == 0) {
                asharia::logError("Editor viewport smoke did not exercise Game View overlay "
                                  "filtering through the coordinator.");
                return false;
            }
        } else if (viewportStats.sceneViewOnlyFlagRequestsDiscarded != 0) {
            asharia::logError(
                "Editor viewport smoke discarded Scene View-only flags unexpectedly.");
            return false;
        }
        if (viewportStats.renderViewDiagnosticsFramesRecorded == 0) {
            asharia::logError("Editor viewport smoke did not record render view diagnostics.");
            return false;
        }
        if (viewportStats.liveRenderGraphSnapshotFrames == 0) {
            asharia::logError("Editor viewport smoke did not draw a live RG View snapshot.");
            return false;
        }
        if (!validateIdleSceneViewReuseSmoke(mode, runResult, viewportStats)) {
            return false;
        }

        asharia::BasicRenderViewDiagnostics scene;
        if (!validateSceneRenderViewDiagnosticsSmoke(viewportHost, scene) ||
            !validateSyntheticMultiViewDiagnosticsSmoke(mode, viewportHost, viewportStats)) {
            return false;
        }
        if (!validateEditorViewportCameraSmoke()) {
            return false;
        }
        return validateRenderViewCameraSmoke(scene);
    }

    bool validateViewportResizeSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult,
                                     const EditorViewportCoordinatorStats& viewportStats,
                                     const ImGuiTextureRegistryStats& textureRegistryStats) {
        if (!isEditorViewportResizeSmokeMode(mode)) {
            return true;
        }
        if (!runResult.resizeRequested || !runResult.resizedViewportPresented) {
            asharia::logError(
                "Editor viewport resize smoke did not present a resized viewport texture.");
            return false;
        }
        if (textureRegistryStats.descriptorsCreated < 2 ||
            textureRegistryStats.descriptorsRetired == 0) {
            asharia::logError(
                "Editor viewport resize smoke did not retire an ImGui texture descriptor.");
            return false;
        }
        if (viewportStats.renderTargetsRetired == 0 || viewportStats.renderTargetsDeferred == 0) {
            asharia::logError("Editor viewport resize smoke did not defer retired viewport "
                              "render target destruction.");
            return false;
        }
        return true;
    }
} // namespace asharia::editor
