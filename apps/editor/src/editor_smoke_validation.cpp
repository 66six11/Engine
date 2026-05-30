#include "editor_smoke_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/log.hpp"
#include "asharia/core/result.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/render_view.hpp"

#include "editor_action.hpp"
#include "editor_command.hpp"
#include "editor_context.hpp"
#include "editor_event.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_input_router.hpp"
#include "editor_panel.hpp"
#include "editor_settings.hpp"
#include "editor_shortcut_router.hpp"
#include "editor_smoke.hpp"
#include "editor_tool.hpp"
#include "editor_ui.hpp"
#include "editor_viewport.hpp"
#include "editor_viewport_coordinator.hpp"
#include "editor_viewport_overlay_provider.hpp"
#include "editor_workspace.hpp"
#include "imgui_runtime.hpp"
#include "imgui_texture_registry.hpp"

namespace asharia::editor {
    bool closeFloat(float lhs, float rhs) {
        return std::fabs(lhs - rhs) < 0.0001F;
    }

    float editorVec3Distance(std::array<float, 3> lhs, std::array<float, 3> rhs) {
        const float deltaX = lhs[0] - rhs[0];
        const float deltaY = lhs[1] - rhs[1];
        const float deltaZ = lhs[2] - rhs[2];
        return std::sqrt((deltaX * deltaX) + (deltaY * deltaY) + (deltaZ * deltaZ));
    }

    bool sameViewportOverlayFlags(EditorViewportOverlayFlags lhs, EditorViewportOverlayFlags rhs) {
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

    [[nodiscard]] bool
    validateRenderViewCameraSmoke(const asharia::BasicRenderViewDiagnostics& diagnostics) {
        if (!closeFloat(diagnostics.camera.position[0], 0.0F) ||
            !closeFloat(diagnostics.camera.position[1], 2.0F) ||
            !closeFloat(diagnostics.camera.position[2], -6.0F) ||
            !closeFloat(diagnostics.camera.nearPlane, 0.1F) ||
            !closeFloat(diagnostics.camera.farPlane, 1000.0F) ||
            diagnostics.camera.projection[0] <= 0.0F || diagnostics.camera.projection[5] <= 1.0F ||
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
            !closeFloat(ray->nearPoint[0], 0.0F) || !closeFloat(ray->nearPoint[1], 1.96837723F) ||
            !closeFloat(ray->nearPoint[2], -5.90513182F) ||
            !closeFloat(editorVec3Distance(camera.position, ray->nearPoint), camera.nearPlane) ||
            !closeFloat(ray->direction[0], 0.0F) || !closeFloat(ray->direction[1], -0.31622776F) ||
            !closeFloat(ray->direction[2], 0.94868332F)) {
            asharia::logError("Editor viewport smoke calculated an invalid center unproject ray.");
            return false;
        }

        const std::optional<EditorViewportWorldRay> topLeftRay =
            unprojectEditorViewportPoint(camera, extent, EditorViewportPoint{.x = 0.0F, .y = 0.0F});
        const std::optional<EditorViewportWorldRay> bottomRightRay =
            unprojectEditorViewportPoint(camera, extent,
                                         EditorViewportPoint{
                                             .x = static_cast<float>(extent.width),
                                             .y = static_cast<float>(extent.height),
                                         });
        if (!topLeftRay || !bottomRightRay || topLeftRay->direction[0] >= 0.0F ||
            topLeftRay->direction[1] <= 0.0F || bottomRightRay->direction[0] <= 0.0F ||
            bottomRightRay->direction[1] >= 0.0F) {
            asharia::logError("Editor viewport smoke found invalid viewport point orientation.");
            return false;
        }

        const float rayLength = std::sqrt((ray->direction[0] * ray->direction[0]) +
                                          (ray->direction[1] * ray->direction[1]) +
                                          (ray->direction[2] * ray->direction[2]));
        const EditorViewportCamera resizedCamera =
            editorViewportCameraForExtent(camera, EditorExtent2D{.width = 640, .height = 320});
        const std::optional<EditorViewportWorldRay> resizedTopLeftRay =
            unprojectEditorViewportPoint(resizedCamera, EditorExtent2D{.width = 640, .height = 320},
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
        return true;
    }

    [[nodiscard]] bool
    validateViewportSmokePresentation(EditorRunMode mode, const EditorSmokeRunResult& runResult,
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
            asharia::logError("Editor viewport smoke did not reuse the idle Scene View texture.");
            return false;
        }
        if (viewportStats.repaintReasonFramesRecorded == 0) {
            asharia::logError("Editor viewport smoke did not record a repaint-reason RenderView.");
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
        if (renderGraphPassTypeCount(scene.renderGraph, asharia::kBasicRenderViewOverlayPassType) !=
                1U ||
            renderGraphOverlayCommandCount(scene.renderGraph) != 4U) {
            asharia::logError(
                "Editor viewport smoke did not record graph-visible RenderView overlay inputs.");
            return false;
        }
        if (scene.viewKind != asharia::BasicRenderViewKind::Scene ||
            scene.frameParams.frameIndex == 0 || !scene.overlay.enabled ||
            scene.overlay.debugWorldLineCount == 0) {
            asharia::logError(
                "Editor viewport smoke recorded invalid RenderView overlay prerequisites.");
            return false;
        }
        const auto debugLineDraw = std::ranges::find_if(
            scene.executionEvents, [](const asharia::BasicRenderViewExecutionEvent& event) {
                return event.kind == asharia::BasicRenderViewExecutionEventKind::Draw &&
                       event.label == "DrawDebugWorldLines";
            });
        if (debugLineDraw == scene.executionEvents.end() ||
            debugLineDraw->draw.vertexCount != scene.overlay.debugWorldLineCount * 2U) {
            asharia::logError(
                "Editor viewport smoke did not record a debug-line overlay draw event.");
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
            viewportStats.viewportRequestsRecorded < 3 ||
            viewportStats.gameViewDiagnosticsFramesRecorded == 0 ||
            viewportStats.previewViewDiagnosticsFramesRecorded == 0) {
            asharia::logError(
                "Editor viewport smoke did not record multiple keyed RenderViews in one frame.");
            return false;
        }

        const std::optional<EditorRecordedRenderViewDiagnostics> gameDiagnostics =
            viewportHost.latestRecordedRenderViewDiagnosticsForView("editor-smoke-game-view",
                                                                    EditorViewportKind::Game);
        const std::optional<EditorRecordedRenderViewDiagnostics> previewDiagnostics =
            viewportHost.latestRecordedRenderViewDiagnosticsForView("editor-smoke-preview-view",
                                                                    EditorViewportKind::Preview);
        if (!gameDiagnostics || !previewDiagnostics) {
            asharia::logError(
                "Editor viewport smoke missed a keyed multi-view diagnostics snapshot.");
            return false;
        }

        const asharia::BasicRenderViewDiagnostics& game = gameDiagnostics->diagnostics;
        if (game.viewKind != asharia::BasicRenderViewKind::Game || !game.overlay.enabled ||
            game.overlay.debugWorldLineCount != 0 ||
            renderGraphPassTypeCount(game.renderGraph, asharia::kBasicRenderViewOverlayPassType) !=
                1U) {
            asharia::logError("Editor viewport smoke recorded invalid Game View diagnostics.");
            return false;
        }

        const asharia::BasicRenderViewDiagnostics& preview = previewDiagnostics->diagnostics;
        if (preview.viewKind != asharia::BasicRenderViewKind::Preview || preview.overlay.enabled ||
            renderGraphPassTypeCount(preview.renderGraph,
                                     asharia::kBasicRenderViewOverlayPassType) != 0U) {
            asharia::logError("Editor viewport smoke leaked overlay inputs into Preview View.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool
    validateViewportFlagsSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult,
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
        if (scenePackets.packets.size() != 1U ||
            scenePackets.packets.front().overlayId != "scene.grid" ||
            scenePackets.packets.front().viewportKind != EditorViewportKind::Scene ||
            scenePackets.debugWorldLineCount() == 0 || !gamePackets.packets.empty()) {
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

    [[nodiscard]] bool
    validateViewportResizeSmoke(EditorRunMode mode, const EditorSmokeRunResult& runResult,
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

    [[nodiscard]] bool validateFrameDebuggerSmoke(EditorRunMode mode,
                                                  const EditorSmokeRunResult& runResult,
                                                  const EditorFrameDebugger& frameDebugger) {
        if (!isEditorFrameDebuggerSmokeMode(mode)) {
            return true;
        }

        if (!runResult.frameDebugCaptureRequested || !runResult.frameDebugReplayPassRequested ||
            !runResult.frameDebugPreviewRequested || !runResult.frameDebugPreviewVisible ||
            !runResult.frameDebugResumeRequested || !runResult.frameDebugRenderedAfterResume) {
            asharia::logError(
                "Editor frame debugger smoke did not complete capture/preview/resume flow.");
            return false;
        }
        if (frameDebugger.state() != EditorFrameDebuggerState::Running) {
            asharia::logError("Editor frame debugger smoke did not return to Running state.");
            return false;
        }

        const EditorFrameDebuggerStats stats = frameDebugger.stats();
        if (stats.captureRequests != 1 || stats.framesCaptured != 1 ||
            stats.completedCaptures != 1 || stats.resumeRequests != 1 || stats.framesResumed != 1 ||
            stats.renderViewFramesSkipped == 0 || stats.previewRequests == 0 ||
            stats.previewFramesRecorded == 0 || stats.previewTextureFramesPublished == 0 ||
            stats.previewTextureFramesDrawn == 0 || stats.replayPassRequests == 0 ||
            stats.replayPassSelections == 0 || stats.replayEventRequests == 0 ||
            stats.replayEventSelections == 0 || stats.frameDebugRenderGraphViewFrames == 0 ||
            stats.frameDebugRenderGraphSnapshotFrames == 0) {
            asharia::logError("Editor frame debugger smoke recorded unexpected state counts.");
            return false;
        }
        if (runResult.viewportFramesAtFrameDebugPreview !=
            runResult.viewportFramesAtFrameDebugPause) {
            asharia::logError(
                "Editor frame debugger smoke recorded a normal RenderView while previewing.");
            return false;
        }
        if (runResult.inspectedWorldFramesAtFrameDebugPreview !=
            runResult.inspectedWorldFramesAtFrameDebugPause) {
            asharia::logError("Editor frame debugger smoke advanced inspected-world safe points "
                              "while previewing.");
            return false;
        }
        if (runResult.viewportFramesAfterFrameDebugResume <=
            runResult.viewportFramesAtFrameDebugPause) {
            asharia::logError("Editor frame debugger smoke did not resume RenderView recording.");
            return false;
        }
        if (runResult.inspectedWorldFramesAfterFrameDebugResume <=
            runResult.inspectedWorldFramesAtFrameDebugPause) {
            asharia::logError(
                "Editor frame debugger smoke did not resume inspected-world safe points.");
            return false;
        }
        const EditorInspectedWorldSchedulerStats& inspectedWorldStats =
            runResult.inspectedWorldStats;
        if (inspectedWorldStats.frameAdvanceSafePoints == 0 ||
            inspectedWorldStats.gameUpdateSafePoints !=
                inspectedWorldStats.frameAdvanceSafePoints ||
            inspectedWorldStats.scriptUpdateSafePoints !=
                inspectedWorldStats.frameAdvanceSafePoints ||
            inspectedWorldStats.skippedFrameAdvanceSafePoints == 0 ||
            inspectedWorldStats.skippedGameUpdateSafePoints !=
                inspectedWorldStats.skippedFrameAdvanceSafePoints ||
            inspectedWorldStats.skippedScriptUpdateSafePoints !=
                inspectedWorldStats.skippedFrameAdvanceSafePoints) {
            asharia::logError(
                "Editor frame debugger smoke recorded invalid inspected-world safe-point counts.");
            return false;
        }
        const std::optional<EditorFrameDebugCapture>& capture = frameDebugger.latestCapture();
        if (!capture) {
            asharia::logError("Editor frame debugger smoke did not keep a captured snapshot.");
            return false;
        }
        if (capture->diagnostics.renderGraph.passes.size() != 3 ||
            capture->diagnostics.renderGraph.resources.size() != 2 ||
            capture->diagnostics.renderGraph.accessEdges.size() != 4 ||
            capture->diagnostics.renderGraph.dependencyEdges.size() != 2 ||
            capture->diagnostics.renderGraph.transitions.size() != 4) {
            asharia::logError(
                "Editor frame debugger smoke captured unexpected RenderGraph diagnostics: passes " +
                std::to_string(capture->diagnostics.renderGraph.passes.size()) + ", resources " +
                std::to_string(capture->diagnostics.renderGraph.resources.size()) +
                ", access edges " +
                std::to_string(capture->diagnostics.renderGraph.accessEdges.size()) +
                ", dependency edges " +
                std::to_string(capture->diagnostics.renderGraph.dependencyEdges.size()) +
                ", transitions " +
                std::to_string(capture->diagnostics.renderGraph.transitions.size()) + ".");
            return false;
        }
        if (capture->diagnostics.executionEvents.empty()) {
            asharia::logError("Editor frame debugger smoke captured no renderer execution events.");
            return false;
        }
        if (frameDebugger.pausedCapture()) {
            asharia::logError("Editor frame debugger smoke kept a paused capture after resume.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateInputRouterSmoke(EditorRunMode mode,
                                                const EditorSmokeRunResult& runResult) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }
        if (runResult.inputStats.capturedFrames <
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor input router smoke did not capture every rendered frame.");
            return false;
        }
        if (runResult.inputStats.sceneViewReports == 0) {
            asharia::logError("Editor input router smoke did not receive Scene View input state.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateShortcutRouterRunSmoke(EditorRunMode mode,
                                                      const EditorSmokeRunResult& runResult) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }
        if (runResult.shortcutStats.evaluatedFrames <
            static_cast<std::uint64_t>(runResult.renderedFrames)) {
            asharia::logError("Editor shortcut router smoke did not evaluate every rendered "
                              "frame.");
            return false;
        }
        if (runResult.shortcutStats.invalidShortcuts != 0) {
            asharia::logError("Editor shortcut router smoke found an invalid registered shortcut.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateImguiLayoutPersistenceSmoke(EditorRunMode mode,
                                                           const ImGuiRuntime& imgui) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }
        if (!imgui.layoutPersistenceEnabled()) {
            asharia::logError("Editor ImGui layout persistence is disabled.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateImguiLayoutSavedSmoke(EditorRunMode mode,
                                                     const ImGuiRuntime& imgui) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }
        if (!std::filesystem::exists(imgui.layoutIniPath())) {
            asharia::logError("Editor ImGui layout smoke did not write the layout ini file.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateI18nSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        if (editorI18nCatalog().empty()) {
            asharia::logError("Editor i18n catalog is empty.");
            return false;
        }

        const EditorI18n enUs{EditorLocale::EnUs};
        const EditorI18n zhHans{EditorLocale::ZhHans};
        const std::string_view enFile = enUs.text("menu.file");
        const std::string_view zhFile = zhHans.text("menu.file");
        if (enFile != "File" || zhFile.empty() || zhFile == enFile) {
            asharia::logError("Editor i18n smoke failed locale text lookup.");
            return false;
        }
        if (zhHans.text(EditorI18nTextQuery{
                .key = "missing.editor.key",
                .fallback = "Fallback",
            }) != "Fallback") {
            asharia::logError("Editor i18n smoke failed missing-key fallback.");
            return false;
        }

        const std::string captureLabel = zhHans.label(EditorI18nLabelDesc{
            .key = "action.debug.captureFrame",
            .stableId = "debug.capture-frame",
            .fallback = "Capture Frame",
        });
        if (!captureLabel.ends_with("###debug.capture-frame")) {
            asharia::logError("Editor i18n smoke failed stable ImGui label id generation.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool validateEditorFontSmoke(EditorRunMode mode, const ImGuiRuntime& imgui,
                                               EditorLocale locale) {
        if (!isEditorSmokeMode(mode) || locale != EditorLocale::ZhHans) {
            return true;
        }

        const ImGuiRuntimeFontStatus& status = imgui.fontStatus();
        if (!status.cjkRequested) {
            asharia::logError("Editor zh-Hans smoke did not request CJK glyph coverage.");
            return false;
        }
        if (status.cjkFontPathExplicit && !status.cjkLoaded) {
            asharia::logError("Editor zh-Hans smoke failed to load the explicit CJK font path.");
            return false;
        }
        if (status.cjkCandidateFound && !status.cjkLoaded) {
            asharia::logError(
                "Editor zh-Hans smoke found a CJK font candidate but did not load it.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool validateEditorThemeSmoke(EditorRunMode mode, EditorUiThemeId expectedTheme) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        const std::span<const EditorUiTheme> themes = editorUiThemes();
        if (themes.size() != 7U) {
            asharia::logError("Editor theme smoke found an unexpected theme count.");
            return false;
        }
        if (defaultEditorUiThemeId() != EditorUiThemeId::ClassicBlueGray ||
            editorUiThemeName(defaultEditorUiThemeId()) != "classic-blue-gray-2") {
            asharia::logError("Editor theme smoke found an invalid default theme.");
            return false;
        }
        const EditorUiTheme& classicTheme = editorUiTheme(EditorUiThemeId::ClassicBlueGray);
        const bool classicAppBgMatches =
            classicTheme.appBackground.r == 0x17U && classicTheme.appBackground.g == 0x1DU &&
            classicTheme.appBackground.b == 0x24U && classicTheme.appBackground.a == 0xFFU;
        const bool classicAccentMatches =
            classicTheme.accent.r == 0x72U && classicTheme.accent.g == 0xB7U &&
            classicTheme.accent.b == 0xE8U && classicTheme.accent.a == 0xFFU;
        if (!classicAppBgMatches || !classicAccentMatches ||
            toImGuiEncodedSrgbU32(classicTheme.appBackground) !=
                IM_COL32(0x17U, 0x1DU, 0x24U, 0xFFU)) {
            asharia::logError("Editor theme smoke found an invalid encoded sRGB theme byte.");
            return false;
        }
        for (std::size_t index = 0; index < themes.size(); ++index) {
            if (themes[index].storageName.empty() || themes[index].name.empty()) {
                asharia::logError("Editor theme smoke found an unnamed theme.");
                return false;
            }
            for (std::size_t otherIndex = index + 1U; otherIndex < themes.size(); ++otherIndex) {
                if (themes[index].storageName == themes[otherIndex].storageName) {
                    asharia::logError("Editor theme smoke found duplicate theme storage names.");
                    return false;
                }
            }
        }
        const std::optional<EditorUiThemeId> defaultTheme =
            editorUiThemeIdFromName("classic-blue-gray-2");
        if (!defaultTheme || *defaultTheme != EditorUiThemeId::ClassicBlueGray) {
            asharia::logError("Editor theme smoke could not resolve the default theme name.");
            return false;
        }
        const std::optional<EditorUiThemeId> legacyDefaultTheme =
            editorUiThemeIdFromName("classic-blue-gray");
        if (!legacyDefaultTheme || *legacyDefaultTheme != EditorUiThemeId::ClassicBlueGray) {
            asharia::logError(
                "Editor theme smoke could not resolve the legacy default theme name.");
            return false;
        }
        if (currentEditorUiThemeId() != expectedTheme) {
            asharia::logError("Editor theme smoke did not apply the startup theme.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateEditorStartupSmoke(EditorRunMode mode, const ImGuiRuntime& imgui,
                                                  EditorLocale locale, EditorUiThemeId theme) {
        return validateImguiLayoutPersistenceSmoke(mode, imgui) && validateI18nSmoke(mode) &&
               validateEditorFontSmoke(mode, imgui, locale) &&
               validateEditorThemeSmoke(mode, theme);
    }

    [[nodiscard]] EditorLocale alternateLocale(EditorLocale locale) {
        return locale == EditorLocale::ZhHans ? EditorLocale::EnUs : EditorLocale::ZhHans;
    }

    [[nodiscard]] EditorUiThemeId alternateTheme(EditorUiThemeId theme) {
        return theme == EditorUiThemeId::WarmGraphiteAmber ? EditorUiThemeId::ClassicBlueGray
                                                           : EditorUiThemeId::WarmGraphiteAmber;
    }

    [[nodiscard]] bool validateEditorSettingsSmoke(EditorRunMode mode,
                                                   EditorContext& editorContext) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        EditorSettingsController& settings = editorContext.settings();
        const EditorLocale initialLocale = settings.settings().locale;
        const EditorUiThemeId initialTheme = settings.settings().theme;
        const EditorLocale changedLocale = alternateLocale(initialLocale);
        if (auto changed = settings.setLocale(changedLocale); !changed) {
            asharia::logError(changed.error().message);
            return false;
        }
        if (settings.settings().locale != changedLocale ||
            editorContext.i18n().locale() != changedLocale) {
            asharia::logError("Editor settings smoke did not apply the selected locale.");
            return false;
        }

        auto loaded = loadEditorSettings(settings.settingsPath(), initialLocale);
        if (!loaded) {
            asharia::logError(loaded.error().message);
            return false;
        }
        if (loaded->locale != changedLocale) {
            asharia::logError("Editor settings smoke did not persist the selected locale.");
            return false;
        }

        const EditorUiThemeId changedTheme = alternateTheme(initialTheme);
        if (auto changed = settings.setTheme(changedTheme); !changed) {
            asharia::logError(changed.error().message);
            return false;
        }
        if (settings.settings().theme != changedTheme || currentEditorUiThemeId() != changedTheme) {
            asharia::logError("Editor settings smoke did not apply the selected theme.");
            return false;
        }

        loaded = loadEditorSettings(settings.settingsPath(), initialLocale);
        if (!loaded) {
            asharia::logError(loaded.error().message);
            return false;
        }
        if (loaded->theme != changedTheme) {
            asharia::logError("Editor settings smoke did not persist the selected theme.");
            return false;
        }

        if (auto restored = settings.setLocale(initialLocale); !restored) {
            asharia::logError(restored.error().message);
            return false;
        }
        if (settings.settings().locale != initialLocale ||
            editorContext.i18n().locale() != initialLocale) {
            asharia::logError("Editor settings smoke did not restore the initial locale.");
            return false;
        }
        if (auto restored = settings.setTheme(initialTheme); !restored) {
            asharia::logError(restored.error().message);
            return false;
        }
        if (settings.settings().theme != initialTheme || currentEditorUiThemeId() != initialTheme) {
            asharia::logError("Editor settings smoke did not restore the initial theme.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool validatePanelRegistrySmoke(EditorPanelRegistry& panelRegistry) {
        constexpr std::size_t kExpectedPanelCount = 6;
        constexpr std::size_t kExpectedOpenPanelCount = 4;

        if (!panelRegistry.closePanel("log") || !panelRegistry.focusPanel("log")) {
            asharia::logError("Editor panel registry smoke could not close and reopen Log panel.");
            return false;
        }
        if (panelRegistry.panelCount() != kExpectedPanelCount ||
            panelRegistry.openPanelCount() != kExpectedOpenPanelCount ||
            !panelRegistry.isOpen("log") || panelRegistry.isOpen("ui-style-preview") ||
            panelRegistry.isOpen("editor-settings")) {
            asharia::logError(
                "Editor panel registry smoke detected invalid singleton panel state.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool validateActionRegistrySmoke(EditorActionRegistry& actionRegistry,
                                                   EditorActionServices& actionServices) {
        constexpr std::size_t kExpectedActionCount = 12;
        constexpr std::size_t kExpectedEnabledActionCount = 9;

        if (actionRegistry.actionCount() != kExpectedActionCount ||
            actionRegistry.enabledActionCount() != kExpectedEnabledActionCount) {
            asharia::logError("Editor action registry smoke detected invalid action counts.");
            return false;
        }
        actionServices.eventQueue.clear();
        if (actionRegistry.invoke("file.open", makeEditorActionInvokeContext(actionServices)) ||
            actionRegistry.invokeCount("file.open") != 0 || !actionServices.eventQueue.empty()) {
            asharia::logError("Editor action registry smoke invoked a disabled action.");
            return false;
        }
        if (!actionServices.panels.closePanel("log") ||
            !actionRegistry.invoke("view.log", makeEditorActionInvokeContext(actionServices)) ||
            !actionServices.panels.isOpen("log") || actionRegistry.invokeCount("view.log") != 1) {
            asharia::logError("Editor action registry smoke failed to route View/Log action.");
            return false;
        }
        const std::span<const EditorEvent> events = actionServices.eventQueue.events();
        const bool closedLog = std::ranges::any_of(events, [](const EditorEvent& event) {
            return event.kind == EditorEventKind::PanelClosed && event.sourceId.value == "log";
        });
        const bool invokedLogAction = std::ranges::any_of(events, [](const EditorEvent& event) {
            return event.kind == EditorEventKind::ActionInvoked &&
                   event.sourceId.value == "view.log";
        });
        const bool openedLog = std::ranges::any_of(events, [](const EditorEvent& event) {
            return event.kind == EditorEventKind::PanelOpened && event.sourceId.value == "log";
        });
        if (!closedLog || !invokedLogAction || !openedLog) {
            asharia::logError("Editor event queue smoke missed action or panel lifecycle events.");
            return false;
        }
        actionServices.eventQueue.clear();

        if (!actionRegistry.invoke("view.ui-style-preview",
                                   makeEditorActionInvokeContext(actionServices)) ||
            !actionServices.panels.isOpen("ui-style-preview") ||
            actionRegistry.invokeCount("view.ui-style-preview") != 1 ||
            !actionServices.panels.closePanel("ui-style-preview")) {
            asharia::logError("Editor action registry smoke failed to route UI Style Preview.");
            return false;
        }
        actionServices.eventQueue.clear();

        if (!actionRegistry.invoke("view.editor-settings",
                                   makeEditorActionInvokeContext(actionServices)) ||
            !actionServices.panels.isOpen("editor-settings") ||
            actionRegistry.invokeCount("view.editor-settings") != 1 ||
            !actionServices.panels.closePanel("editor-settings")) {
            asharia::logError("Editor action registry smoke failed to route Editor Settings.");
            return false;
        }
        actionServices.eventQueue.clear();

        if (!actionRegistry.invoke("view.reset-layout",
                                   makeEditorActionInvokeContext(actionServices)) ||
            actionRegistry.invokeCount("view.reset-layout") != 1 ||
            actionServices.workspace.layoutResetRequestCount() != 1) {
            asharia::logError("Editor action registry smoke failed to request layout reset.");
            return false;
        }
        actionServices.eventQueue.clear();

        return true;
    }

    [[nodiscard]] bool validateToolRegistrySmoke(const EditorToolRegistry& toolRegistry,
                                                 const EditorActionRegistry& actionRegistry,
                                                 const EditorPanelRegistry& panelRegistry) {
        constexpr std::size_t kExpectedToolCount = 7;
        constexpr std::size_t kExpectedPanelContributions = 6;
        constexpr std::size_t kExpectedActionContributions = 9;
        constexpr std::size_t kExpectedToolbarActionContributions = 8;
        constexpr std::size_t kExpectedViewportOverlayContributions = 3;

        if (toolRegistry.toolCount() != kExpectedToolCount ||
            toolRegistry.panelContributionCount() != kExpectedPanelContributions ||
            toolRegistry.actionContributionCount() != kExpectedActionContributions ||
            toolRegistry.toolbarActionContributionCount() != kExpectedToolbarActionContributions ||
            toolRegistry.viewportOverlayContributionCount() !=
                kExpectedViewportOverlayContributions) {
            asharia::logError("Editor tool registry smoke detected invalid contribution counts.");
            return false;
        }

        bool referencesValid = true;
        toolRegistry.visitTools([&](const EditorToolDesc& tool) {
            for (const EditorToolPanelContribution& panel : tool.panels) {
                referencesValid =
                    referencesValid && panelRegistry.findPanelDesc(panel.panelId) != nullptr;
            }
            for (const EditorToolActionContribution& action : tool.actions) {
                referencesValid =
                    referencesValid && actionRegistry.findAction(action.actionId) != nullptr;
            }
            for (const EditorToolViewportOverlayContribution& overlay : tool.viewportOverlays) {
                referencesValid = referencesValid && overlay.viewportId == "scene-view";
            }
        });
        if (!referencesValid) {
            asharia::logError("Editor tool registry smoke found an invalid contribution target.");
            return false;
        }

        bool sawDebugToolbarAction = false;
        bool sawViewToolbarAction = false;
        bool sawUtilityToolbarAction = false;
        toolRegistry.visitToolbarActions(
            EditorToolbarSlot::Debug,
            [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                static_cast<void>(tool);
                sawDebugToolbarAction =
                    sawDebugToolbarAction || action.actionId == "debug.capture-frame";
            });
        toolRegistry.visitToolbarActions(
            EditorToolbarSlot::View,
            [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                static_cast<void>(tool);
                sawViewToolbarAction = sawViewToolbarAction || action.actionId == "view.scene-view";
            });
        toolRegistry.visitToolbarActions(
            EditorToolbarSlot::Utility,
            [&](const EditorToolDesc& tool, const EditorToolActionContribution& action) {
                static_cast<void>(tool);
                sawUtilityToolbarAction =
                    sawUtilityToolbarAction || action.actionId == "view.editor-settings";
            });
        if (!sawDebugToolbarAction || !sawViewToolbarAction || !sawUtilityToolbarAction) {
            asharia::logError("Editor tool registry smoke missed a toolbar contribution slot.");
            return false;
        }

        std::size_t sceneOverlayCount = 0;
        bool sawSceneGrid = false;
        bool sawSceneGizmo = false;
        bool sawSceneSelectionOutline = false;
        toolRegistry.visitViewportOverlays(
            "scene-view",
            [&](const EditorToolDesc& tool, const EditorToolViewportOverlayContribution& overlay) {
                static_cast<void>(tool);
                ++sceneOverlayCount;
                sawSceneGrid = sawSceneGrid || overlay.overlayId == "scene.grid";
                sawSceneGizmo = sawSceneGizmo || overlay.overlayId == "scene.transform-gizmo";
                sawSceneSelectionOutline =
                    sawSceneSelectionOutline || overlay.overlayId == "scene.selection-outline";
            });
        std::size_t gameOverlayCount = 0;
        toolRegistry.visitViewportOverlays(
            "game-view",
            [&](const EditorToolDesc& tool, const EditorToolViewportOverlayContribution& overlay) {
                static_cast<void>(tool);
                static_cast<void>(overlay);
                ++gameOverlayCount;
            });
        if (sceneOverlayCount != kExpectedViewportOverlayContributions || gameOverlayCount != 0 ||
            !sawSceneGrid || !sawSceneGizmo || !sawSceneSelectionOutline) {
            asharia::logError("Editor tool registry smoke missed a viewport overlay query.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool validateShortcutRouterSmoke(EditorActionRegistry& actionRegistry,
                                                   EditorActionServices& actionServices) {
        EditorShortcutRouter shortcutRouter;
        actionServices.eventQueue.clear();

        shortcutRouter.beginFrame(EditorInputSnapshot{
            .shortcutsEnabled = false,
        });
        if (shortcutRouter.routeShortcut(
                actionRegistry, makeEditorActionInvokeContext(actionServices), "view.log", true) ||
            actionRegistry.invokeCount("view.log") != 1 || !actionServices.eventQueue.empty()) {
            asharia::logError("Editor shortcut router smoke invoked while shortcuts were "
                              "disabled.");
            return false;
        }

        shortcutRouter.beginFrame(EditorInputSnapshot{
            .shortcutsEnabled = true,
        });
        if (shortcutRouter.routeShortcut(
                actionRegistry, makeEditorActionInvokeContext(actionServices), "file.open", true) ||
            actionRegistry.invokeCount("file.open") != 0 || !actionServices.eventQueue.empty()) {
            asharia::logError("Editor shortcut router smoke invoked a disabled action.");
            return false;
        }

        if (!actionServices.panels.closePanel("log") ||
            !shortcutRouter.routeShortcut(
                actionRegistry, makeEditorActionInvokeContext(actionServices), "view.log", true) ||
            !actionServices.panels.isOpen("log") || actionRegistry.invokeCount("view.log") != 2) {
            asharia::logError("Editor shortcut router smoke failed to invoke View/Log.");
            return false;
        }

        const EditorShortcutRouterStats stats = shortcutRouter.stats();
        if (stats.evaluatedFrames != 2 || stats.blockedFrames != 1 || stats.shortcutMatches != 2 ||
            stats.shortcutInvocations != 1) {
            asharia::logError("Editor shortcut router smoke detected invalid shortcut stats.");
            return false;
        }
        actionServices.eventQueue.clear();

        return true;
    }

    [[nodiscard]] bool validateEditorRegistrationSmoke(EditorRunMode mode,
                                                       EditorActionRegistry& actionRegistry,
                                                       EditorContext& editorContext,
                                                       EditorActionServices& actionServices,
                                                       const EditorToolRegistry& toolRegistry) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        return validatePanelRegistrySmoke(actionServices.panels) &&
               validateActionRegistrySmoke(actionRegistry, actionServices) &&
               validateToolRegistrySmoke(toolRegistry, actionRegistry, actionServices.panels) &&
               validateEditorSettingsSmoke(mode, editorContext) &&
               validateShortcutRouterSmoke(actionRegistry, actionServices);
    }

    class TestSetIntCommand final : public EditorCommand {
    public:
        TestSetIntCommand(int& target, int newValue)
            : target_(&target), newValue_(newValue), oldValue_(target) {}

        [[nodiscard]] std::string description() const override {
            return "SetInt " + std::to_string(oldValue_) + " -> " + std::to_string(newValue_);
        }

        [[nodiscard]] asharia::Result<void> execute() override {
            *target_ = newValue_;
            return {};
        }

        [[nodiscard]] asharia::Result<void> undo() override {
            *target_ = oldValue_;
            return {};
        }

    private:
        int* target_{};
        int newValue_{};
        int oldValue_{};
    };

    [[nodiscard]] bool validateEditorCommandSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        int testValue = 0;
        constexpr int kNewValue = 42;
        EditorCommandHistory history;
        {
            auto transaction = EditorTransaction{};
            transaction.addCommand(std::make_unique<TestSetIntCommand>(testValue, kNewValue));
            history.push(std::move(transaction));
        }
        if (history.undoDepth() != 1 || history.redoDepth() != 0) {
            asharia::logError("Editor command smoke: invalid depth after push.");
            return false;
        }
        auto undoResult = history.undo();
        if (!undoResult || testValue != 0 || history.undoDepth() != 0 || history.redoDepth() != 1) {
            asharia::logError("Editor command smoke: undo did not restore value.");
            return false;
        }
        auto redoResult = history.redo();
        if (!redoResult || testValue != kNewValue || history.undoDepth() != 1 ||
            history.redoDepth() != 0) {
            asharia::logError("Editor command smoke: redo did not reapply value.");
            return false;
        }
        auto emptyUndo = history.undo();
        auto doubleUndo = history.undo();
        if (doubleUndo) {
            asharia::logError("Editor command smoke: double undo should have failed.");
            return false;
        }
        static_cast<void>(emptyUndo);
        return true;
    }
} // namespace asharia::editor
