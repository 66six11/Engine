#include "editor_app.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/core/result.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderers.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_action.hpp"
#include "editor_app_registration.hpp"
#include "editor_command.hpp"
#include "editor_event.hpp"
#include "editor_frame_debugger.hpp"
#include "editor_i18n.hpp"
#include "editor_input_router.hpp"
#include "editor_inspected_world.hpp"
#include "editor_panel.hpp"
#include "editor_settings.hpp"
#include "editor_shortcut_router.hpp"
#include "editor_smoke.hpp"
#include "editor_smoke_validation.hpp"
#include "editor_tool.hpp"
#include "editor_viewport.hpp"
#include "editor_viewport_coordinator.hpp"
#include "editor_viewport_overlay_provider.hpp"
#include "editor_workspace.hpp"
#include "imgui_editor_shell.hpp"
#include "imgui_frame_renderer.hpp"
#include "imgui_runtime.hpp"

namespace {

    constexpr asharia::VulkanDebugLabelMode kEditorDebugLabels =
        asharia::VulkanDebugLabelMode::Required;
    constexpr int kSmokeAttemptLimit = 120;

    [[nodiscard]] std::filesystem::path editorSmokeLayoutIniPath() {
        std::error_code error;
        std::filesystem::path basePath = std::filesystem::temp_directory_path(error);
        if (error) {
            basePath = std::filesystem::current_path(error);
        }
        if (basePath.empty()) {
            basePath = ".";
        }
        return basePath / "Asharia" / "EditorSmoke" / "imgui-layout.ini";
    }

    [[nodiscard]] std::filesystem::path editorI18nDirectory() {
#if defined(ASHARIA_EDITOR_I18N_DIR)
        return std::filesystem::path{ASHARIA_EDITOR_I18N_DIR};
#else
        return std::filesystem::path{"resources/i18n"};
#endif
    }

    [[nodiscard]] std::filesystem::path editorLayoutIniPathForRun(bool smokeMode) {
        if (smokeMode) {
            return editorSmokeLayoutIniPath();
        }
        return {};
    }

    [[nodiscard]] std::filesystem::path editorSettingsPathForRun(bool smokeMode) {
        return smokeMode ? asharia::editor::editorSmokeSettingsPath()
                         : asharia::editor::editorUserSettingsPath();
    }

    struct EditorSettingsRunState {
        asharia::editor::EditorSettings settings;
        std::filesystem::path path;
    };

    [[nodiscard]] EditorSettingsRunState
    loadEditorSettingsForRun(bool smokeMode, asharia::editor::EditorLocale fallbackLocale) {
        EditorSettingsRunState state{
            .settings =
                asharia::editor::EditorSettings{
                    .locale = fallbackLocale,
                    .theme = asharia::editor::defaultEditorUiThemeId(),
                },
            .path = editorSettingsPathForRun(smokeMode),
        };
        if (smokeMode) {
            std::error_code removeError;
            std::filesystem::remove(state.path, removeError);
        }

        auto loaded = asharia::editor::loadEditorSettings(state.path, fallbackLocale);
        if (loaded) {
            state.settings = *loaded;
        } else {
            asharia::logError(loaded.error().message);
        }
        return state;
    }

    [[nodiscard]] std::string editorLocaleEnvironmentValue() {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t valueSize = 0;
        if (_dupenv_s(&value, &valueSize, "ASHARIA_EDITOR_LOCALE") != 0 || value == nullptr) {
            return {};
        }
        const std::unique_ptr<char, decltype(&std::free)> ownedValue{value, &std::free};
        return std::string{ownedValue.get()};
#else
        const char* value = std::getenv("ASHARIA_EDITOR_LOCALE");
        return value == nullptr ? std::string{} : std::string{value};
#endif
    }

    [[nodiscard]] asharia::editor::EditorLocale editorLocaleFromEnvironment() {
        const std::string value = editorLocaleEnvironmentValue();
        if (value.empty()) {
            return asharia::editor::EditorLocale::EnUs;
        }
        const std::optional<asharia::editor::EditorLocale> locale =
            asharia::editor::editorLocaleFromName(value);
        if (locale) {
            return *locale;
        }

        asharia::logError("Unsupported ASHARIA_EDITOR_LOCALE value: " + value);
        return asharia::editor::EditorLocale::EnUs;
    }

    bool isRenderableExtent(asharia::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, asharia::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    asharia::editor::EditorExtent2D editorExtentFromVk(VkExtent2D extent) {
        return asharia::editor::EditorExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    void buildEditorShell(asharia::editor::EditorActionRegistry& actionRegistry,
                          asharia::editor::EditorActionServices& actionServices,
                          asharia::editor::EditorFrameDebugger& frameDebugger,
                          asharia::editor::EditorI18n& i18n,
                          asharia::editor::EditorPanelRegistry& panelRegistry,
                          asharia::editor::EditorToolRegistry& toolRegistry,
                          asharia::editor::EditorWorkspaceController& workspace,
                          asharia::editor::EditorFrameContext& frameContext) {
        const asharia::editor::EditorActionInvokeContext actionInvoke =
            asharia::editor::makeEditorActionInvokeContext(actionServices);
        auto dockspaceContext = asharia::editor::EditorDockspaceContext{
            .panels = panelRegistry,
            .i18n = i18n,
            .workspace = workspace,
        };
        const auto menuContext = asharia::editor::EditorMenuContext{
            .panels = panelRegistry,
            .i18n = i18n,
            .actionInvoke = actionInvoke,
        };
        const auto commandBarContext = asharia::editor::EditorCommandBarContext{
            .i18n = i18n,
            .tools = toolRegistry,
            .actionInvoke = actionInvoke,
        };
        const auto statusBarContext = asharia::editor::EditorStatusBarContext{
            .frame = frameContext,
            .panels = panelRegistry,
            .frameDebugger = frameDebugger,
        };

        asharia::editor::drawEditorMainMenu(actionRegistry, menuContext);
        asharia::editor::drawEditorCommandBar(actionRegistry, commandBarContext);
        asharia::editor::drawEditorStatusBar(statusBarContext);
        asharia::editor::drawEditorDockspace(dockspaceContext);
        panelRegistry.drawPanels(frameContext);
    }

    [[nodiscard]] asharia::VoidResult waitForRenderableWindow(asharia::GlfwWindow& window,
                                                              bool smokeMode) {
        int attempts = 0;
        auto framebuffer = window.framebufferExtent();
        while (!window.shouldClose() && !isRenderableExtent(framebuffer)) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{
                    asharia::vulkanError("Timed out waiting for a renderable editor framebuffer")};
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            asharia::GlfwWindow::pollEvents();
            framebuffer = window.framebufferExtent();
        }

        return {};
    }

    [[nodiscard]] asharia::Result<asharia::VulkanContext>
    createEditorVulkanContext(const std::vector<std::string>& extensions,
                              asharia::GlfwWindow& window) {
        const asharia::VulkanContextDesc contextDesc{
            .applicationName = "Asharia Engine Editor",
            .requiredInstanceExtensions = extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return asharia::glfwCreateVulkanSurface(window, instance);
                },
            .debugLabels = kEditorDebugLabels,
        };

        return asharia::VulkanContext::create(contextDesc);
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameLoop>
    createEditorFrameLoop(const asharia::VulkanContext& context,
                          const asharia::GlfwWindow& window) {
        const auto framebuffer = window.framebufferExtent();
        return asharia::VulkanFrameLoop::create(
            context, asharia::VulkanFrameLoopDesc{
                         .width = framebuffer.width,
                         .height = framebuffer.height,
                         .clearColor = VkClearColorValue{{0.015F, 0.018F, 0.022F, 1.0F}},
                     });
    }

    [[nodiscard]] asharia::Result<bool>
    prepareFrameLoopExtent(asharia::GlfwWindow& window, asharia::VulkanFrameLoop& frameLoop) {
        const auto currentFramebuffer = window.framebufferExtent();
        frameLoop.setTargetExtent(currentFramebuffer.width, currentFramebuffer.height);
        if (!isRenderableExtent(currentFramebuffer)) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            return false;
        }

        if (!extentMatches(frameLoop.extent(), currentFramebuffer)) {
            auto recreated = frameLoop.recreate();
            if (!recreated) {
                return std::unexpected{std::move(recreated.error())};
            }
            if (*recreated == asharia::VulkanFrameStatus::OutOfDate) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(16ms);
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] asharia::Result<bool>
    renderEditorFrame(asharia::VulkanFrameLoop& frameLoop,
                      asharia::BasicFullscreenTextureRenderer& renderer,
                      asharia::editor::EditorViewportCoordinator& viewportHost,
                      asharia::editor::EditorFrameDebugger& frameDebugger, int frameIndex) {
        auto status =
            frameLoop.renderFrame([&renderer, &viewportHost, &frameDebugger,
                                   frameIndex](const asharia::VulkanFrameRecordContext& context)
                                      -> asharia::Result<asharia::VulkanFrameRecordResult> {
                const bool recordRenderViews = frameDebugger.shouldRecordRenderViews();
                asharia::editor::EditorViewportRepaintReasons repaintReasons =
                    frameDebugger.consumeRenderViewRepaintReasons();
                if (frameDebugger.isCapturingFrame()) {
                    asharia::editor::addEditorViewportRepaintReason(
                        repaintReasons,
                        asharia::editor::EditorViewportRepaintReason::FrameDebugEventChanged);
                }
                auto viewport = viewportHost.recordRequestedViews(
                    context, renderer, recordRenderViews, repaintReasons);
                if (!viewport) {
                    return std::unexpected{std::move(viewport.error())};
                }
                auto frameDebugPreview =
                    viewportHost.recordFrameDebugPreview(context, renderer, frameDebugger);
                if (!frameDebugPreview) {
                    return std::unexpected{std::move(frameDebugPreview.error())};
                }
                if (!recordRenderViews) {
                    frameDebugger.notifyRenderViewSkipped();
                } else if (frameDebugger.isCapturingFrame()) {
                    const auto& diagnostics = viewportHost.latestRecordedRenderViewDiagnostics();
                    if (diagnostics) {
                        frameDebugger.captureRecordedView(
                            asharia::editor::EditorFrameDebugCaptureDesc{
                                .frameIndex = frameIndex,
                                .submittedFrameEpoch = diagnostics->submittedFrameEpoch,
                                .viewKind = diagnostics->kind,
                                .requestedExtent = diagnostics->requestedExtent,
                                .diagnostics = diagnostics->diagnostics,
                            });
                    }
                }

                return asharia::editor::recordEditorImguiFrame(context);
            });
        if (!status) {
            return std::unexpected{std::move(status.error())};
        }

        return *status != asharia::VulkanFrameStatus::OutOfDate;
    }

    [[nodiscard]] asharia::Result<asharia::editor::EditorSmokeRunResult>
    runEditorLoop(asharia::GlfwWindow& window, asharia::VulkanFrameLoop& frameLoop,
                  asharia::BasicFullscreenTextureRenderer& renderer,
                  asharia::editor::EditorViewportCoordinator& viewportHost,
                  asharia::editor::EditorFrameDebugger& frameDebugger,
                  asharia::editor::EditorActionRegistry& actionRegistry,
                  asharia::editor::EditorActionServices& actionServices,
                  asharia::editor::EditorEventQueue& eventQueue,
                  asharia::editor::EditorDiagnosticsLog& diagnosticsLog,
                  asharia::editor::EditorI18n& i18n,
                  asharia::editor::EditorSettingsController& settingsController,
                  asharia::editor::EditorPanelRegistry& panelRegistry,
                  asharia::editor::EditorToolRegistry& toolRegistry,
                  asharia::editor::EditorWorkspaceController& workspace,
                  asharia::editor::EditorRunMode mode) {
        const bool smokeMode = asharia::editor::isEditorSmokeMode(mode);
        asharia::editor::EditorViewportResizeSmokeState resizeSmoke;
        asharia::editor::EditorFrameDebuggerSmokeState frameDebugSmoke;
        asharia::editor::EditorInspectedWorldScheduler inspectedWorldScheduler;
        asharia::editor::EditorInputRouter inputRouter;
        asharia::editor::EditorShortcutRouter shortcutRouter;
        int renderedFrames = 0;
        int attempts = 0;
        while (!window.shouldClose()) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{asharia::vulkanError(
                    "Editor shell smoke timed out before rendering enough frames")};
            }

            asharia::GlfwWindow::pollEvents();
            auto extentReady = prepareFrameLoopExtent(window, frameLoop);
            if (!extentReady) {
                return std::unexpected{std::move(extentReady.error())};
            }
            if (!*extentReady) {
                continue;
            }

            if (asharia::editor::isEditorFrameDebuggerSmokeMode(mode)) {
                asharia::editor::updateFrameDebuggerSmoke(frameDebugger, actionRegistry,
                                                          actionServices, viewportHost,
                                                          inspectedWorldScheduler, frameDebugSmoke);
            }
            frameDebugger.beginFrame(renderedFrames);
            inspectedWorldScheduler.runFrameSafePoints(
                frameDebugger.shouldRunInspectedWorldSafePoints());
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            const ImGuiIO& imguiIo = ImGui::GetIO();
            inputRouter.beginFrame(asharia::editor::EditorInputCapture{
                .imguiWantsMouse = imguiIo.WantCaptureMouse,
                .imguiWantsKeyboard = imguiIo.WantCaptureKeyboard,
                .imguiWantsTextInput = imguiIo.WantTextInput,
            });
            viewportHost.beginImguiFrame(asharia::editor::editorViewportFrameEpochs(frameLoop));
            panelRegistry.clearLifecycleEvents();
            eventQueue.clear();
            asharia::editor::EditorFrameContext frameContext{
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
                .tools = {.registry = toolRegistry},
                .input = {.router = inputRouter},
                .renderGraph = {.snapshots = viewportHost},
                .viewport = {.host = viewportHost},
            };
            buildEditorShell(actionRegistry, actionServices, frameDebugger, i18n, panelRegistry,
                             toolRegistry, workspace, frameContext);
            asharia::editor::requestSyntheticMultiViewSmoke(mode, viewportHost);
            inputRouter.finalizeFrame();
            shortcutRouter.beginFrame(inputRouter.snapshot());
            static_cast<void>(shortcutRouter.routeImGuiShortcuts(
                actionRegistry, asharia::editor::makeEditorActionInvokeContext(actionServices)));
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
            if (asharia::editor::isEditorViewportResizeSmokeMode(mode)) {
                asharia::editor::updateViewportResizeSmoke(window, viewportHost, resizeSmoke);
            }
            if (asharia::editor::isEditorFrameDebuggerSmokeMode(mode)) {
                asharia::editor::updateFrameDebuggerSmoke(frameDebugger, actionRegistry,
                                                          actionServices, viewportHost,
                                                          inspectedWorldScheduler, frameDebugSmoke);
            }
            diagnosticsLog.appendEvents(eventQueue.events());
            eventQueue.clear();
            panelRegistry.clearLifecycleEvents();

            if (smokeMode && renderedFrames >= asharia::editor::editorSmokeFrameCount(mode)) {
                break;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(smokeMode ? 16ms : 1ms);
        }

        return asharia::editor::EditorSmokeRunResult{
            .renderedFrames = renderedFrames,
            .resizeRequested = resizeSmoke.requested,
            .resizedViewportPresented = resizeSmoke.presentedAfterResize,
            .frameDebugCaptureRequested = frameDebugSmoke.captureRequested,
            .frameDebugReplayPassRequested = frameDebugSmoke.replayPassRequested,
            .frameDebugPreviewRequested = frameDebugSmoke.previewRequested,
            .frameDebugPreviewVisible = frameDebugSmoke.previewVisible,
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

} // namespace

namespace asharia::editor {

    int runEditor(EditorRunMode mode) {
        const bool smokeMode = asharia::editor::isEditorSmokeMode(mode);

        auto glfw = asharia::GlfwInstance::create();
        if (!glfw) {
            asharia::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = asharia::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            asharia::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = asharia::GlfwWindow::create(
            *glfw, asharia::WindowDesc{.title = "Asharia Engine Editor"});
        if (!window) {
            asharia::logError(window.error().message);
            return EXIT_FAILURE;
        }

        auto vulkanContext = createEditorVulkanContext(*extensions, *window);
        if (!vulkanContext) {
            asharia::logError(vulkanContext.error().message);
            return EXIT_FAILURE;
        }

        asharia::GlfwWindow::pollEvents();
        if (auto waited = waitForRenderableWindow(*window, smokeMode); !waited) {
            asharia::logError(waited.error().message);
            return EXIT_FAILURE;
        }
        if (window->shouldClose()) {
            return EXIT_SUCCESS;
        }

        auto frameLoop = createEditorFrameLoop(*vulkanContext, *window);
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        if (auto loaded = asharia::editor::loadEditorI18nCatalog(editorI18nDirectory()); !loaded) {
            asharia::logError(loaded.error().message);
            return EXIT_FAILURE;
        }

        const asharia::editor::EditorLocale fallbackLocale = editorLocaleFromEnvironment();
        const EditorSettingsRunState settingsRun =
            loadEditorSettingsForRun(smokeMode, fallbackLocale);
        const asharia::editor::EditorLocale editorLocale = settingsRun.settings.locale;

        ImGuiRuntime imgui;
        const ImGuiRuntimeDesc imguiDesc{
            .layoutIniPath = editorLayoutIniPathForRun(smokeMode),
            .theme = settingsRun.settings.theme,
            .enableCjkGlyphs = true,
            .cjkFontPath = {},
            .fontPixelSize = 16.0F,
        };
        if (auto created =
                imgui.create(window->nativeHandle(), *vulkanContext, *frameLoop, imguiDesc);
            !created) {
            asharia::logError(created.error().message);
            return EXIT_FAILURE;
        }
        if (!validateEditorStartupSmoke(mode, imgui, editorLocale, settingsRun.settings.theme)) {
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer = asharia::BasicFullscreenTextureRenderer::create(
            asharia::BasicFullscreenTextureRendererDesc{
                .device = vulkanContext->device(),
                .allocator = vulkanContext->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            asharia::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        EditorViewportCoordinator viewportHost;
        if (auto created = viewportHost.create(*vulkanContext); !created) {
            asharia::logError(created.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorEventQueue eventQueue;
        asharia::editor::EditorDiagnosticsLog diagnosticsLog;
        asharia::editor::EditorFrameDebugger frameDebugger;
        asharia::editor::EditorI18n editorI18n{settingsRun.settings.locale};
        asharia::editor::EditorSettingsController settingsController{settingsRun.settings,
                                                                     settingsRun.path, editorI18n};
        asharia::editor::EditorWorkspaceController workspaceController;
        asharia::editor::EditorPanelRegistry panelRegistry;
        panelRegistry.setEventQueue(&eventQueue);
        if (auto registered = registerEditorPanels(panelRegistry); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorActionRegistry actionRegistry;
        if (auto registered = registerEditorActions(actionRegistry); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorToolRegistry toolRegistry;
        if (auto registered = registerEditorTools(toolRegistry); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorActionServices actionServices{
            .eventQueue = eventQueue,
            .panels = panelRegistry,
            .frameDebugger = frameDebugger,
            .workspace = workspaceController,
        };
        if (!validateEditorRegistrationSmoke(mode, actionRegistry, actionServices,
                                             settingsController, editorI18n, toolRegistry) ||
            !validateEditorCommandSmoke(mode)) {
            return EXIT_FAILURE;
        }

        auto runResult = runEditorLoop(*window, *frameLoop, *renderer, viewportHost, frameDebugger,
                                       actionRegistry, actionServices, eventQueue, diagnosticsLog,
                                       editorI18n, settingsController, panelRegistry, toolRegistry,
                                       workspaceController, mode);
        if (!runResult) {
            asharia::logError(runResult.error().message);
            return EXIT_FAILURE;
        }
        if (smokeMode && workspaceController.layoutApplyCount() == 0) {
            asharia::logError("Editor workspace smoke did not apply a dock layout preset.");
            return EXIT_FAILURE;
        }
        const asharia::editor::EditorViewportCoordinatorStats viewportStats = viewportHost.stats();
        const asharia::editor::ImGuiTextureRegistryStats textureRegistryStats =
            viewportHost.textureRegistryStats();
        if (!validateViewportSmokePresentation(mode, *runResult, viewportHost,
                                               textureRegistryStats) ||
            !validateViewportFlagsSmoke(mode, *runResult, viewportHost, viewportStats) ||
            !validateViewportResizeSmoke(mode, *runResult, viewportStats, textureRegistryStats) ||
            !validateFrameDebuggerSmoke(mode, *runResult, frameDebugger) ||
            !validateInputRouterSmoke(mode, *runResult) ||
            !validateShortcutRouterRunSmoke(mode, *runResult)) {
            return EXIT_FAILURE;
        }
        imgui.saveLayoutNow();
        if (!validateImguiLayoutSavedSmoke(mode, imgui)) {
            return EXIT_FAILURE;
        }

        const VkExtent2D viewportExtent = viewportHost.descriptorExtent();
        viewportHost.shutdown();
        imgui.shutdown();
        window->requestClose();

        std::cout << "Editor shell frames: " << runResult->renderedFrames
                  << ", viewport frames: " << viewportHost.viewportFramesRendered()
                  << ", texture frames: " << viewportHost.textureFramesSubmitted()
                  << ", input frames: " << runResult->inputStats.capturedFrames
                  << ", scene input reports: " << runResult->inputStats.sceneViewReports
                  << ", shortcut frames: " << runResult->shortcutStats.evaluatedFrames
                  << ", shortcut invocations: " << runResult->shortcutStats.shortcutInvocations
                  << ", overlay texture frames: " << viewportStats.overlayFlagTextureFramesAcquired
                  << ", render view diagnostics frames: "
                  << viewportStats.renderViewDiagnosticsFramesRecorded
                  << ", idle scene skips: " << viewportStats.idleSceneViewFramesSkipped
                  << ", frame debugger: " << frameDebugger.stateName()
                  << ", live texture descriptors peak: " << textureRegistryStats.peakLiveDescriptors
                  << ", viewport: " << viewportExtent.width << 'x' << viewportExtent.height << '\n';
        if (asharia::editor::isEditorViewportResizeSmokeMode(mode)) {
            std::cout << "Editor viewport resize: " << runResult->viewportExtentBeforeResize.width
                      << 'x' << runResult->viewportExtentBeforeResize.height << " -> "
                      << runResult->viewportExtentAfterResize.width << 'x'
                      << runResult->viewportExtentAfterResize.height
                      << ", retired render targets: " << viewportStats.renderTargetsRetired
                      << ", deferred render targets: " << viewportStats.renderTargetsDeferred
                      << ", retired texture descriptors: "
                      << textureRegistryStats.descriptorsRetired << '\n';
        }
        return EXIT_SUCCESS;
    }

} // namespace asharia::editor
