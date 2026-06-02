#include "editor_loop_host.hpp"

#include <chrono>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <thread>
#include <utility>

#include "asharia/renderer_basic_vulkan/basic_renderers.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_action.hpp"
#include "editor_event.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_input_router.hpp"
#include "editor_inspected_world.hpp"
#include "editor_panel.hpp"
#include "editor_settings.hpp"
#include "editor_shell_host.hpp"
#include "editor_shortcut_router.hpp"
#include "editor_tool.hpp"
#include "editor_tool_manager.hpp"
#include "editor_viewport_coordinator.hpp"
#include "editor_viewport_overlay_provider.hpp"
#include "editor_vulkan_host.hpp"
#include "editor_workspace.hpp"

namespace asharia::editor {
    namespace {

        constexpr int kSmokeAttemptLimit = 120;

    } // namespace

    [[nodiscard]] Result<EditorSmokeRunResult>
    runEditorLoop(GlfwWindow& window, VulkanFrameLoop& frameLoop,
                  BasicFullscreenTextureRenderer& renderer, EditorViewportCoordinator& viewportHost,
                  EditorFrameDebugger& frameDebugger, EditorActionRegistry& actionRegistry,
                  EditorActionServices& actionServices, EditorEventQueue& eventQueue,
                  EditorDiagnosticsLog& diagnosticsLog, EditorI18n& i18n,
                  EditorSettingsController& settingsController, EditorPanelRegistry& panelRegistry,
                  EditorToolRegistry& toolRegistry, EditorToolManager& toolManager,
                  EditorWorkspaceController& workspace, EditorRunMode mode) {
        const bool smokeMode = isEditorSmokeMode(mode);
        EditorViewportResizeSmokeState resizeSmoke;
        EditorFrameDebuggerSmokeState frameDebugSmoke;
        EditorInspectedWorldScheduler inspectedWorldScheduler;
        EditorInputRouter inputRouter;
        EditorShortcutRouter shortcutRouter;
        int renderedFrames = 0;
        int attempts = 0;
        while (!window.shouldClose()) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{
                    vulkanError("Editor shell smoke timed out before rendering enough frames")};
            }

            GlfwWindow::pollEvents();
            auto extentReady = prepareEditorFrameLoopExtent(window, frameLoop);
            if (!extentReady) {
                return std::unexpected{std::move(extentReady.error())};
            }
            if (!*extentReady) {
                continue;
            }

            if (isEditorFrameDebuggerSmokeMode(mode)) {
                updateFrameDebuggerSmoke(frameDebugger, actionRegistry, actionServices,
                                         viewportHost, inspectedWorldScheduler, frameDebugSmoke);
            }
            frameDebugger.beginFrame(renderedFrames);
            inspectedWorldScheduler.runFrameSafePoints(
                frameDebugger.shouldRunInspectedWorldSafePoints());
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            const ImGuiIO& imguiIo = ImGui::GetIO();
            inputRouter.beginFrame(EditorInputCapture{
                .imguiWantsMouse = imguiIo.WantCaptureMouse,
                .imguiWantsKeyboard = imguiIo.WantCaptureKeyboard,
                .imguiWantsTextInput = imguiIo.WantTextInput,
            });
            viewportHost.beginImguiFrame(editorViewportFrameEpochs(frameLoop));
            panelRegistry.clearLifecycleEvents();
            eventQueue.clear();
            EditorFrameContext frameContext{
                .ui =
                    {
                        .frameIndex = renderedFrames,
                        .swapchainExtent = editorExtentFromVk(frameLoop.extent()),
                        .smokeMode = smokeMode,
                        .i18n = i18n,
                    },
                .diagnostics =
                    {
                        .log = diagnosticsLog,
                        .frameDebugger = frameDebugger,
                    },
                .settings = {.controller = settingsController},
                .tools = {.registry = toolRegistry, .manager = toolManager},
                .input = {.router = inputRouter},
                .renderGraph = {.snapshots = viewportHost},
                .viewport = {.host = viewportHost},
            };
            drawEditorShellFrame(actionRegistry, actionServices, frameDebugger, i18n, panelRegistry,
                                 toolRegistry, workspace, frameContext.ui);
            panelRegistry.drawPanels(frameContext);
            requestSyntheticMultiViewSmoke(mode, viewportHost);
            inputRouter.finalizeFrame();
            shortcutRouter.beginFrame(inputRouter.snapshot());
            static_cast<void>(shortcutRouter.routeImGuiShortcuts(
                actionRegistry, makeEditorActionInvokeContext(actionServices)));
            ImGui::Render();

            auto rendered =
                renderEditorFrame(frameLoop, renderer, viewportHost, frameDebugger, renderedFrames);
            if (!rendered) {
                return std::unexpected{std::move(rendered.error())};
            }
            if (*rendered) {
                ++renderedFrames;
            }
            frameDebugger.endSubmittedFrame(frameLoop.completedFrameEpoch());
            if (isEditorViewportResizeSmokeMode(mode)) {
                updateViewportResizeSmoke(window, viewportHost, resizeSmoke);
            }
            if (isEditorFrameDebuggerSmokeMode(mode)) {
                updateFrameDebuggerSmoke(frameDebugger, actionRegistry, actionServices,
                                         viewportHost, inspectedWorldScheduler, frameDebugSmoke);
            }
            diagnosticsLog.appendEvents(eventQueue.events());
            eventQueue.clear();
            panelRegistry.clearLifecycleEvents();

            if (smokeMode && renderedFrames >= editorSmokeFrameCount(mode)) {
                break;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(smokeMode ? 16ms : 1ms);
        }

        return EditorSmokeRunResult{
            .renderedFrames = renderedFrames,
            .resizeRequested = resizeSmoke.requested,
            .resizedViewportPresented = resizeSmoke.presentedAfterResize,
            .frameDebugCaptureRequested = frameDebugSmoke.captureRequested,
            .frameDebugReplayPassRequested = frameDebugSmoke.replayPassRequested,
            .frameDebugPreviewRequested = frameDebugSmoke.previewRequested,
            .frameDebugPreviewVisible = frameDebugSmoke.previewVisible,
            .frameDebugPreviewSelectedPassIndex = frameDebugSmoke.previewSelectedPassIndex,
            .frameDebugPreviewSelectedExecutionEventId =
                frameDebugSmoke.previewSelectedExecutionEventId,
            .frameDebugPreviewCopiedAfterPassIndex = frameDebugSmoke.previewCopiedAfterPassIndex,
            .frameDebugResumeRequested = frameDebugSmoke.resumeRequested,
            .frameDebugRenderedAfterResume = frameDebugSmoke.renderedAfterResume,
            .viewportExtentBeforeResize = resizeSmoke.extentBeforeResize,
            .viewportExtentAfterResize = resizeSmoke.extentAfterResize,
            .textureFramesBeforeResize = resizeSmoke.textureFramesBeforeResize,
            .viewportFramesAtFrameDebugPause = frameDebugSmoke.viewportFramesAtPause,
            .viewportFramesAtFrameDebugPreview = frameDebugSmoke.viewportFramesAtPreview,
            .viewportFramesAfterFrameDebugResume = frameDebugSmoke.viewportFramesAfterResume,
            .inspectedWorldFramesAtFrameDebugPause = frameDebugSmoke.inspectedWorldFramesAtPause,
            .inspectedWorldFramesAtFrameDebugPreview =
                frameDebugSmoke.inspectedWorldFramesAtPreview,
            .inspectedWorldFramesAfterFrameDebugResume =
                frameDebugSmoke.inspectedWorldFramesAfterResume,
            .inspectedWorldStats = inspectedWorldScheduler.stats(),
            .inputStats = inputRouter.stats(),
            .shortcutStats = shortcutRouter.stats(),
        };
    }

} // namespace asharia::editor
