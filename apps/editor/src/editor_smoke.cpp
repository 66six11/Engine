#include "editor_smoke.hpp"

#include <cstddef>
#include <optional>

#include "asharia/renderer_basic_vulkan/render_view.hpp"
#include "asharia/rendergraph/render_graph_compile.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_action.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_viewport.hpp"
#include "editor_viewport_coordinator.hpp"

namespace {

    constexpr int kSmokeFrameCount = 3;
    constexpr int kResizeSmokeFrameCount = 8;
    constexpr int kFrameDebuggerSmokeFrameCount = 7;
    constexpr int kResizeSmokeWindowWidth = 960;
    constexpr int kResizeSmokeWindowHeight = 540;

    [[nodiscard]] bool isRenderableExtent(VkExtent2D extent) {
        return extent.width > 0 && extent.height > 0;
    }

    [[nodiscard]] bool differs(VkExtent2D lhs, VkExtent2D rhs) {
        return lhs.width != rhs.width || lhs.height != rhs.height;
    }

    [[nodiscard]] std::uint64_t extentArea(VkExtent2D extent) {
        return static_cast<std::uint64_t>(extent.width) * static_cast<std::uint64_t>(extent.height);
    }

    std::optional<std::size_t>
    chooseFrameDebugReplayPass(const asharia::editor::EditorFrameDebugger& frameDebugger) {
        const std::optional<asharia::editor::EditorFrameDebugCapture>& capture =
            frameDebugger.pausedCapture();
        if (!capture) {
            return std::nullopt;
        }

        std::optional<std::size_t> fallback;
        for (const asharia::RenderGraphDiagnosticsPassNode& pass :
             capture->diagnostics.renderGraph.passes) {
            if (!fallback) {
                fallback = pass.passIndex;
            }
            if (pass.name == "ClearFullscreenSource") {
                return pass.passIndex;
            }
        }
        return fallback;
    }

    std::optional<asharia::BasicRenderViewExecutionEventId>
    chooseFrameDebugReplayEvent(const asharia::editor::EditorFrameDebugger& frameDebugger) {
        const std::optional<asharia::editor::EditorFrameDebugCapture>& capture =
            frameDebugger.pausedCapture();
        if (!capture) {
            return std::nullopt;
        }

        std::optional<asharia::BasicRenderViewExecutionEventId> fallback;
        for (const asharia::BasicRenderViewExecutionEvent& event :
             capture->diagnostics.executionEvents) {
            if (event.kind == asharia::BasicRenderViewExecutionEventKind::BeginPass ||
                event.kind == asharia::BasicRenderViewExecutionEventKind::EndPass) {
                continue;
            }
            if (!fallback) {
                fallback = event.id;
            }
            if (event.passName == "FullscreenTexture" &&
                event.kind == asharia::BasicRenderViewExecutionEventKind::DrawFullscreenTriangle) {
                return event.id;
            }
        }
        return fallback;
    }

    void captureFrameDebugPreviewSmokeState(
        asharia::editor::EditorFrameDebuggerSmokeState& state,
        const asharia::editor::EditorFrameDebugPreview& preview,
        const asharia::editor::EditorViewportCoordinator& viewportHost,
        const asharia::editor::EditorInspectedWorldScheduler& inspectedWorldScheduler) {
        state.previewSelectedPassIndex = preview.selectedPassIndex;
        state.previewSelectedExecutionEventId.reset();
        if (preview.selectedExecutionEventId) {
            state.previewSelectedExecutionEventId = preview.selectedExecutionEventId->value;
        }
        state.previewSelectedImageResourceIndex = preview.selectedImageResourceIndex;
        state.previewCopiedAfterPassIndex = preview.copiedAfterPassIndex;
        state.viewportFramesAtPreview = viewportHost.viewportFramesRendered();
        state.inspectedWorldFramesAtPreview =
            inspectedWorldScheduler.stats().frameAdvanceSafePoints;
        state.previewVisible = true;
    }

} // namespace

namespace asharia::editor {

    bool isEditorSmokeMode(EditorRunMode mode) {
        return mode != EditorRunMode::Interactive;
    }

    bool isEditorViewportSmokeMode(EditorRunMode mode) {
        return mode == EditorRunMode::SmokeViewport || mode == EditorRunMode::SmokeViewportResize ||
               mode == EditorRunMode::SmokeFrameDebugger;
    }

    bool isEditorViewportResizeSmokeMode(EditorRunMode mode) {
        return mode == EditorRunMode::SmokeViewportResize;
    }

    bool isEditorFrameDebuggerSmokeMode(EditorRunMode mode) {
        return mode == EditorRunMode::SmokeFrameDebugger;
    }

    bool isEditorAssetBrowserSmokeMode(EditorRunMode mode) {
        return mode == EditorRunMode::SmokeAssetBrowser;
    }

    int editorSmokeFrameCount(EditorRunMode mode) {
        if (isEditorViewportResizeSmokeMode(mode)) {
            return kResizeSmokeFrameCount;
        }
        if (isEditorFrameDebuggerSmokeMode(mode)) {
            return kFrameDebuggerSmokeFrameCount;
        }
        return kSmokeFrameCount;
    }

    void requestSyntheticMultiViewSmoke(EditorRunMode mode,
                                        EditorViewportCoordinator& viewportHost) {
        if (mode != EditorRunMode::SmokeViewport ||
            viewportHost.stats().multiViewFramesRecorded > 0) {
            return;
        }

        const EditorViewportOverlayFlags allFlags{
            .gridVisible = true,
            .gizmoVisible = true,
            .wireVisible = true,
            .selectionOutlineVisible = true,
            .debugOverlayVisible = true,
            .debugGizmoVisible = true,
        };
        const EditorViewportOverlayFlags customSceneGridFlags{
            .gridVisible = true,
        };
        const EditorExtent2D gameExtent{.width = 192, .height = 128};
        const EditorExtent2D previewExtent{.width = 96, .height = 96};
        const EditorExtent2D customSceneExtent{.width = 128, .height = 96};
        const EditorViewportWorldGridSettings customSceneGrid{
            .planeY = 0.5F,
            .minorSpacing = 2.0F,
            .majorSpacing = 20.0F,
            .fadeStart = 8.0F,
            .fadeEnd = 80.0F,
            .opacity = 0.65F,
            .color = {0.58F, 0.62F, 0.72F, 0.8F},
        };
        viewportHost.requestViewport(EditorViewportRequest{
            .panelId = EditorId{.value = "editor-smoke-custom-grid-scene-view"},
            .kind = EditorViewportKind::Scene,
            .extent = customSceneExtent,
            .camera = defaultEditorSceneViewCamera(customSceneExtent),
            .overlayFlags = customSceneGridFlags,
            .worldGrid = customSceneGrid,
            .refresh =
                EditorViewportRefreshRequest{
                    .policy = EditorViewportRefreshPolicy::OnDemand,
                },
        });
        viewportHost.requestViewport(EditorViewportRequest{
            .panelId = EditorId{.value = "editor-smoke-game-view"},
            .kind = EditorViewportKind::Game,
            .extent = gameExtent,
            .camera = defaultEditorSceneViewCamera(gameExtent),
            .overlayFlags = allFlags,
            .worldGrid = {},
            .refresh =
                EditorViewportRefreshRequest{
                    .policy = EditorViewportRefreshPolicy::OnDemand,
                },
        });
        viewportHost.requestViewport(EditorViewportRequest{
            .panelId = EditorId{.value = "editor-smoke-preview-view"},
            .kind = EditorViewportKind::Preview,
            .extent = previewExtent,
            .camera = defaultEditorSceneViewCamera(previewExtent),
            .overlayFlags = allFlags,
            .worldGrid = {},
            .refresh =
                EditorViewportRefreshRequest{
                    .policy = EditorViewportRefreshPolicy::OnDemand,
                },
        });
    }

    void updateViewportResizeSmoke(GlfwWindow& window,
                                   const EditorViewportCoordinator& viewportHost,
                                   EditorViewportResizeSmokeState& state) {
        if (!state.requested && viewportHost.hasPresentedViewportTexture()) {
            state.extentBeforeResize = viewportHost.descriptorExtent();
            state.textureFramesBeforeResize = viewportHost.textureFramesSubmitted();
            window.setSize(kResizeSmokeWindowWidth, kResizeSmokeWindowHeight);
            state.requested = true;
        }

        const VkExtent2D currentViewportExtent = viewportHost.descriptorExtent();
        if (state.requested && !state.presentedAfterResize &&
            isRenderableExtent(currentViewportExtent) &&
            differs(currentViewportExtent, state.extentBeforeResize) &&
            extentArea(currentViewportExtent) < extentArea(state.extentBeforeResize) &&
            viewportHost.textureFramesSubmitted() > state.textureFramesBeforeResize) {
            state.extentAfterResize = currentViewportExtent;
            state.presentedAfterResize = true;
        }
    }

    void updateFrameDebuggerSmoke(EditorFrameDebugger& frameDebugger,
                                  EditorActionRegistry& actionRegistry,
                                  EditorActionServices& actionServices,
                                  const EditorViewportCoordinator& viewportHost,
                                  const EditorInspectedWorldScheduler& inspectedWorldScheduler,
                                  EditorFrameDebuggerSmokeState& state) {
        if (!state.captureRequested) {
            state.captureRequested = actionRegistry.invoke(
                "debug.capture-frame", makeEditorActionInvokeContext(actionServices));
            return;
        }

        if (!state.resumeRequested &&
            frameDebugger.state() == EditorFrameDebuggerState::PausedFrameDebug) {
            if (!state.replayPassRequested) {
                state.viewportFramesAtPause = viewportHost.viewportFramesRendered();
                state.inspectedWorldFramesAtPause =
                    inspectedWorldScheduler.stats().frameAdvanceSafePoints;
                const std::optional<BasicRenderViewExecutionEventId> replayEvent =
                    chooseFrameDebugReplayEvent(frameDebugger);
                if (replayEvent) {
                    state.replayPassRequested = frameDebugger.selectReplayEvent(*replayEvent);
                    state.previewRequested = state.replayPassRequested;
                } else {
                    const std::optional<std::size_t> replayPass =
                        chooseFrameDebugReplayPass(frameDebugger);
                    if (replayPass) {
                        state.replayPassRequested = frameDebugger.selectReplayPass(*replayPass);
                        state.previewRequested = state.replayPassRequested;
                    }
                }
                return;
            }

            if (!state.previewRequested) {
                return;
            }

            const EditorFrameDebugPreview& preview = frameDebugger.preview();
            const EditorFrameDebuggerStats stats = frameDebugger.stats();
            if (!state.previewVisible) {
                if (preview.status == EditorFrameDebugPreviewStatus::Available &&
                    hasEditorViewportTexture(preview.texture) &&
                    stats.previewTextureFramesDrawn > 0) {
                    captureFrameDebugPreviewSmokeState(state, preview, viewportHost,
                                                       inspectedWorldScheduler);
                }
                return;
            }

            state.resumeRequested = actionRegistry.invoke(
                "debug.resume-frame", makeEditorActionInvokeContext(actionServices));
            return;
        }

        if (state.resumeRequested && !state.renderedAfterResume &&
            frameDebugger.state() == EditorFrameDebuggerState::Running &&
            viewportHost.viewportFramesRendered() > state.viewportFramesAtPause) {
            state.viewportFramesAfterResume = viewportHost.viewportFramesRendered();
            state.inspectedWorldFramesAfterResume =
                inspectedWorldScheduler.stats().frameAdvanceSafePoints;
            state.renderedAfterResume = true;
        }
    }

} // namespace asharia::editor
