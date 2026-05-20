#include "editor_app.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/core/result.hpp"
#include "asharia/renderer_basic_vulkan/basic_triangle_renderer.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_action.hpp"
#include "editor_context.hpp"
#include "editor_input_router.hpp"
#include "editor_panel.hpp"
#include "editor_shortcut_router.hpp"
#include "editor_viewport.hpp"
#include "editor_viewport_coordinator.hpp"
#include "imgui_editor_shell.hpp"
#include "imgui_runtime.hpp"
#include "panels/log_panel.hpp"
#include "panels/scene_view_panel.hpp"

namespace {

    constexpr asharia::VulkanDebugLabelMode kEditorDebugLabels =
        asharia::VulkanDebugLabelMode::Required;
    constexpr int kSmokeFrameCount = 3;
    constexpr int kResizeSmokeFrameCount = 8;
    constexpr int kSmokeAttemptLimit = 120;
    constexpr int kResizeSmokeWindowWidth = 960;
    constexpr int kResizeSmokeWindowHeight = 540;

    bool isSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode != asharia::editor::EditorRunMode::Interactive;
    }

    bool isViewportSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode == asharia::editor::EditorRunMode::SmokeViewport ||
               mode == asharia::editor::EditorRunMode::SmokeViewportResize;
    }

    bool isViewportResizeSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode == asharia::editor::EditorRunMode::SmokeViewportResize;
    }

    int smokeFrameCount(asharia::editor::EditorRunMode mode) {
        if (isViewportResizeSmokeMode(mode)) {
            return kResizeSmokeFrameCount;
        }
        return kSmokeFrameCount;
    }

    bool isRenderableExtent(asharia::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, asharia::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    bool isRenderableExtent(VkExtent2D extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool differs(VkExtent2D lhs, VkExtent2D rhs) {
        return lhs.width != rhs.width || lhs.height != rhs.height;
    }

    std::uint64_t extentArea(VkExtent2D extent) {
        return static_cast<std::uint64_t>(extent.width) * static_cast<std::uint64_t>(extent.height);
    }

    asharia::editor::EditorExtent2D editorExtentFromVk(VkExtent2D extent) {
        return asharia::editor::EditorExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    struct ImageBarrierDesc {
        VkImage image{VK_NULL_HANDLE};
        VkImageLayout oldLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImageLayout newLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkPipelineStageFlags2 srcStageMask{};
        VkAccessFlags2 srcAccessMask{};
        VkPipelineStageFlags2 dstStageMask{};
        VkAccessFlags2 dstAccessMask{};
    };

    struct EditorSmokeRunResult {
        int renderedFrames{};
        bool resizeRequested{};
        bool resizedViewportPresented{};
        VkExtent2D viewportExtentBeforeResize{};
        VkExtent2D viewportExtentAfterResize{};
        std::uint64_t textureFramesBeforeResize{};
        asharia::editor::EditorInputRouterStats inputStats;
        asharia::editor::EditorShortcutRouterStats shortcutStats;
    };

    struct EditorViewportResizeSmokeState {
        bool requested{};
        bool presentedAfterResize{};
        VkExtent2D extentBeforeResize{};
        VkExtent2D extentAfterResize{};
        std::uint64_t textureFramesBeforeResize{};
    };

    void updateViewportResizeSmoke(asharia::GlfwWindow& window,
                                   const asharia::editor::EditorViewportCoordinator& viewportHost,
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

    [[nodiscard]] bool validateViewportSmokePresentation(
        asharia::editor::EditorRunMode mode, const EditorSmokeRunResult& runResult,
        const asharia::editor::EditorViewportCoordinator& viewportHost,
        const asharia::editor::ImGuiTextureRegistryStats& textureRegistryStats) {
        if (!isViewportSmokeMode(mode)) {
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
        if (textureRegistryStats.peakLiveDescriptors >
            asharia::editor::kEditorViewportTextureDescriptorBudget) {
            asharia::logError("Editor viewport smoke exceeded ImGui texture descriptor budget.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validateViewportResizeSmoke(
        asharia::editor::EditorRunMode mode, const EditorSmokeRunResult& runResult,
        const asharia::editor::EditorViewportCoordinatorStats& viewportStats,
        const asharia::editor::ImGuiTextureRegistryStats& textureRegistryStats) {
        if (!isViewportResizeSmokeMode(mode)) {
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

    [[nodiscard]] bool validateInputRouterSmoke(asharia::editor::EditorRunMode mode,
                                                const EditorSmokeRunResult& runResult) {
        if (!isSmokeMode(mode)) {
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

    [[nodiscard]] bool validateShortcutRouterRunSmoke(asharia::editor::EditorRunMode mode,
                                                      const EditorSmokeRunResult& runResult) {
        if (!isSmokeMode(mode)) {
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

    VkImageMemoryBarrier2 imageBarrier(const ImageBarrierDesc& desc) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = desc.srcStageMask;
        barrier.srcAccessMask = desc.srcAccessMask;
        barrier.dstStageMask = desc.dstStageMask;
        barrier.dstAccessMask = desc.dstAccessMask;
        barrier.oldLayout = desc.oldLayout;
        barrier.newLayout = desc.newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = desc.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        return barrier;
    }

    void cmdPipelineBarrier(VkCommandBuffer commandBuffer, const VkImageMemoryBarrier2& barrier) {
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    void buildEditorShell(asharia::editor::EditorActionRegistry& actionRegistry,
                          asharia::editor::EditorContext& editorContext,
                          asharia::editor::EditorPanelRegistry& panelRegistry,
                          asharia::editor::EditorFrameContext& frameContext) {
        asharia::editor::drawEditorDockspace();
        asharia::editor::drawEditorMainMenu(actionRegistry, editorContext);
        panelRegistry.drawPanels(frameContext);
    }

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
    recordEditorImguiFrame(const asharia::VulkanFrameRecordContext& context) {
        const VkImageMemoryBarrier2 colorBarrier = imageBarrier(ImageBarrierDesc{
            .image = context.image,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        });
        cmdPipelineBarrier(context.commandBuffer, colorBarrier);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = context.imageView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = context.clearColor;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = context.extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(context.commandBuffer, &renderingInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), context.commandBuffer);
        vkCmdEndRendering(context.commandBuffer);

        const VkImageMemoryBarrier2 presentBarrier = imageBarrier(ImageBarrierDesc{
            .image = context.image,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = 0,
        });
        cmdPipelineBarrier(context.commandBuffer, presentBarrier);

        return asharia::VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
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
    createEditorContext(const std::vector<std::string>& extensions, asharia::GlfwWindow& window) {
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
                      asharia::editor::EditorViewportCoordinator& viewportHost) {
        auto status = frameLoop.renderFrame(
            [&renderer, &viewportHost](const asharia::VulkanFrameRecordContext& context)
                -> asharia::Result<asharia::VulkanFrameRecordResult> {
                auto viewport = viewportHost.recordRequestedViews(context, renderer);
                if (!viewport) {
                    return std::unexpected{std::move(viewport.error())};
                }

                return recordEditorImguiFrame(context);
            });
        if (!status) {
            return std::unexpected{std::move(status.error())};
        }

        return *status != asharia::VulkanFrameStatus::OutOfDate;
    }

    [[nodiscard]] asharia::Result<EditorSmokeRunResult>
    runEditorLoop(asharia::GlfwWindow& window, asharia::VulkanFrameLoop& frameLoop,
                  asharia::BasicFullscreenTextureRenderer& renderer,
                  asharia::editor::EditorViewportCoordinator& viewportHost,
                  asharia::editor::EditorActionRegistry& actionRegistry,
                  asharia::editor::EditorContext& editorContext,
                  asharia::editor::EditorPanelRegistry& panelRegistry,
                  asharia::editor::EditorRunMode mode) {
        const bool smokeMode = isSmokeMode(mode);
        EditorViewportResizeSmokeState resizeSmoke;
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
            editorContext.eventQueue().clear();
            asharia::editor::EditorFrameContext frameContext{
                .frameIndex = renderedFrames,
                .swapchainExtent = editorExtentFromVk(frameLoop.extent()),
                .smokeMode = smokeMode,
                .eventQueue = editorContext.eventQueue(),
                .diagnosticsLog = editorContext.diagnosticsLog(),
                .inputRouter = inputRouter,
                .viewportHost = viewportHost,
            };
            buildEditorShell(actionRegistry, editorContext, panelRegistry, frameContext);
            inputRouter.finalizeFrame();
            shortcutRouter.beginFrame(inputRouter.snapshot());
            static_cast<void>(shortcutRouter.routeImGuiShortcuts(actionRegistry, editorContext));
            ImGui::Render();

            auto rendered = renderEditorFrame(frameLoop, renderer, viewportHost);
            if (!rendered) {
                return std::unexpected{std::move(rendered.error())};
            }
            if (*rendered) {
                ++renderedFrames;
            }
            if (isViewportResizeSmokeMode(mode)) {
                updateViewportResizeSmoke(window, viewportHost, resizeSmoke);
            }
            editorContext.diagnosticsLog().appendEvents(editorContext.eventQueue().events());
            editorContext.eventQueue().clear();
            panelRegistry.clearLifecycleEvents();

            if (smokeMode && renderedFrames >= smokeFrameCount(mode)) {
                break;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(smokeMode ? 16ms : 1ms);
        }

        return EditorSmokeRunResult{
            .renderedFrames = renderedFrames,
            .resizeRequested = resizeSmoke.requested,
            .resizedViewportPresented = resizeSmoke.presentedAfterResize,
            .viewportExtentBeforeResize = resizeSmoke.extentBeforeResize,
            .viewportExtentAfterResize = resizeSmoke.extentAfterResize,
            .textureFramesBeforeResize = resizeSmoke.textureFramesBeforeResize,
            .inputStats = inputRouter.stats(),
            .shortcutStats = shortcutRouter.stats(),
        };
    }

    [[nodiscard]] asharia::VoidResult
    registerEditorPanels(asharia::editor::EditorPanelRegistry& panelRegistry) {
        auto sceneView = panelRegistry.registerPanel(
            [] { return std::make_unique<asharia::editor::SceneViewPanel>(); });
        if (!sceneView) {
            return std::unexpected{std::move(sceneView.error())};
        }

        return panelRegistry.registerPanel(
            [] { return std::make_unique<asharia::editor::LogPanel>(); });
    }

    [[nodiscard]] asharia::VoidResult
    registerEditorActions(asharia::editor::EditorActionRegistry& actionRegistry) {
        auto newScene = actionRegistry.registerAction(asharia::editor::EditorActionDesc{
            .id = asharia::editor::EditorId{.value = "file.new-scene"},
            .menuPath = "File",
            .label = "New Scene",
            .shortcut = "Ctrl+N",
            .enabled = false,
        });
        if (!newScene) {
            return std::unexpected{std::move(newScene.error())};
        }

        auto openScene = actionRegistry.registerAction(asharia::editor::EditorActionDesc{
            .id = asharia::editor::EditorId{.value = "file.open"},
            .menuPath = "File",
            .label = "Open...",
            .shortcut = "Ctrl+O",
            .enabled = false,
        });
        if (!openScene) {
            return std::unexpected{std::move(openScene.error())};
        }

        auto exit = actionRegistry.registerAction(asharia::editor::EditorActionDesc{
            .id = asharia::editor::EditorId{.value = "file.exit"},
            .menuPath = "File",
            .label = "Exit",
            .shortcut = "Alt+F4",
            .enabled = false,
        });
        if (!exit) {
            return std::unexpected{std::move(exit.error())};
        }

        auto sceneView = actionRegistry.registerAction(
            asharia::editor::EditorActionDesc{
                .id = asharia::editor::EditorId{.value = "view.scene-view"},
                .menuPath = "View",
                .label = "Scene View",
                .shortcut = "Ctrl+1",
                .enabled = true,
            },
            [](asharia::editor::EditorContext& context) {
                static_cast<void>(context.panelRegistry().focusPanel("scene-view"));
            });
        if (!sceneView) {
            return std::unexpected{std::move(sceneView.error())};
        }

        return actionRegistry.registerAction(
            asharia::editor::EditorActionDesc{
                .id = asharia::editor::EditorId{.value = "view.log"},
                .menuPath = "View",
                .label = "Log",
                .shortcut = "Ctrl+2",
                .enabled = true,
            },
            [](asharia::editor::EditorContext& context) {
                static_cast<void>(context.panelRegistry().focusPanel("log"));
            });
    }

    [[nodiscard]] bool
    validatePanelRegistrySmoke(asharia::editor::EditorPanelRegistry& panelRegistry) {
        constexpr std::size_t kExpectedPanelCount = 2;

        if (!panelRegistry.closePanel("log") || !panelRegistry.focusPanel("log")) {
            asharia::logError("Editor panel registry smoke could not close and reopen Log panel.");
            return false;
        }
        if (panelRegistry.panelCount() != kExpectedPanelCount ||
            panelRegistry.openPanelCount() != kExpectedPanelCount || !panelRegistry.isOpen("log")) {
            asharia::logError(
                "Editor panel registry smoke detected invalid singleton panel state.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool
    validateActionRegistrySmoke(asharia::editor::EditorActionRegistry& actionRegistry,
                                asharia::editor::EditorContext& editorContext,
                                asharia::editor::EditorPanelRegistry& panelRegistry) {
        constexpr std::size_t kExpectedActionCount = 5;
        constexpr std::size_t kExpectedEnabledActionCount = 2;

        if (actionRegistry.actionCount() != kExpectedActionCount ||
            actionRegistry.enabledActionCount() != kExpectedEnabledActionCount) {
            asharia::logError("Editor action registry smoke detected invalid action counts.");
            return false;
        }
        editorContext.eventQueue().clear();
        if (actionRegistry.invoke("file.open", editorContext) ||
            actionRegistry.invokeCount("file.open") != 0 || !editorContext.eventQueue().empty()) {
            asharia::logError("Editor action registry smoke invoked a disabled action.");
            return false;
        }
        if (!panelRegistry.closePanel("log") || !actionRegistry.invoke("view.log", editorContext) ||
            !panelRegistry.isOpen("log") || actionRegistry.invokeCount("view.log") != 1) {
            asharia::logError("Editor action registry smoke failed to route View/Log action.");
            return false;
        }
        const std::span<const asharia::editor::EditorEvent> events =
            editorContext.eventQueue().events();
        const bool closedLog =
            std::ranges::any_of(events, [](const asharia::editor::EditorEvent& event) {
                return event.kind == asharia::editor::EditorEventKind::PanelClosed &&
                       event.sourceId.value == "log";
            });
        const bool invokedLogAction =
            std::ranges::any_of(events, [](const asharia::editor::EditorEvent& event) {
                return event.kind == asharia::editor::EditorEventKind::ActionInvoked &&
                       event.sourceId.value == "view.log";
            });
        const bool openedLog =
            std::ranges::any_of(events, [](const asharia::editor::EditorEvent& event) {
                return event.kind == asharia::editor::EditorEventKind::PanelOpened &&
                       event.sourceId.value == "log";
            });
        if (!closedLog || !invokedLogAction || !openedLog) {
            asharia::logError("Editor event queue smoke missed action or panel lifecycle events.");
            return false;
        }
        editorContext.eventQueue().clear();

        return true;
    }

    [[nodiscard]] bool
    validateShortcutRouterSmoke(asharia::editor::EditorActionRegistry& actionRegistry,
                                asharia::editor::EditorContext& editorContext,
                                asharia::editor::EditorPanelRegistry& panelRegistry) {
        asharia::editor::EditorShortcutRouter shortcutRouter;
        editorContext.eventQueue().clear();

        shortcutRouter.beginFrame(asharia::editor::EditorInputSnapshot{
            .shortcutsEnabled = false,
        });
        if (shortcutRouter.routeShortcut(actionRegistry, editorContext, "view.log", true) ||
            actionRegistry.invokeCount("view.log") != 1 || !editorContext.eventQueue().empty()) {
            asharia::logError("Editor shortcut router smoke invoked while shortcuts were "
                              "disabled.");
            return false;
        }

        shortcutRouter.beginFrame(asharia::editor::EditorInputSnapshot{
            .shortcutsEnabled = true,
        });
        if (shortcutRouter.routeShortcut(actionRegistry, editorContext, "file.open", true) ||
            actionRegistry.invokeCount("file.open") != 0 || !editorContext.eventQueue().empty()) {
            asharia::logError("Editor shortcut router smoke invoked a disabled action.");
            return false;
        }

        if (!panelRegistry.closePanel("log") ||
            !shortcutRouter.routeShortcut(actionRegistry, editorContext, "view.log", true) ||
            !panelRegistry.isOpen("log") || actionRegistry.invokeCount("view.log") != 2) {
            asharia::logError("Editor shortcut router smoke failed to invoke View/Log.");
            return false;
        }

        const asharia::editor::EditorShortcutRouterStats stats = shortcutRouter.stats();
        if (stats.evaluatedFrames != 2 || stats.blockedFrames != 1 ||
            stats.shortcutMatches != 2 || stats.shortcutInvocations != 1) {
            asharia::logError("Editor shortcut router smoke detected invalid shortcut stats.");
            return false;
        }
        editorContext.eventQueue().clear();

        return true;
    }

} // namespace

namespace asharia::editor {

    int runEditor(EditorRunMode mode) {
        const bool smokeMode = isSmokeMode(mode);

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

        auto context = createEditorContext(*extensions, *window);
        if (!context) {
            asharia::logError(context.error().message);
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

        auto frameLoop = createEditorFrameLoop(*context, *window);
        if (!frameLoop) {
            asharia::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        ImGuiRuntime imgui;
        if (auto created = imgui.create(window->nativeHandle(), *context, *frameLoop); !created) {
            asharia::logError(created.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{ASHARIA_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer = asharia::BasicFullscreenTextureRenderer::create(
            asharia::BasicFullscreenTextureRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            asharia::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        EditorViewportCoordinator viewportHost;
        if (auto created = viewportHost.create(*context); !created) {
            asharia::logError(created.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorEventQueue eventQueue;
        asharia::editor::EditorDiagnosticsLog diagnosticsLog;
        asharia::editor::EditorPanelRegistry panelRegistry;
        panelRegistry.setEventQueue(&eventQueue);
        if (auto registered = registerEditorPanels(panelRegistry); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }
        if (smokeMode && !validatePanelRegistrySmoke(panelRegistry)) {
            return EXIT_FAILURE;
        }

        asharia::editor::EditorActionRegistry actionRegistry;
        if (auto registered = registerEditorActions(actionRegistry); !registered) {
            asharia::logError(registered.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorContext editorContext{panelRegistry, eventQueue, diagnosticsLog};
        if (smokeMode &&
            !validateActionRegistrySmoke(actionRegistry, editorContext, panelRegistry)) {
            return EXIT_FAILURE;
        }
        if (smokeMode &&
            !validateShortcutRouterSmoke(actionRegistry, editorContext, panelRegistry)) {
            return EXIT_FAILURE;
        }

        auto runResult = runEditorLoop(*window, *frameLoop, *renderer, viewportHost, actionRegistry,
                                       editorContext, panelRegistry, mode);
        if (!runResult) {
            asharia::logError(runResult.error().message);
            return EXIT_FAILURE;
        }
        const asharia::editor::EditorViewportCoordinatorStats viewportStats = viewportHost.stats();
        const asharia::editor::ImGuiTextureRegistryStats textureRegistryStats =
            viewportHost.textureRegistryStats();
        if (!validateViewportSmokePresentation(mode, *runResult, viewportHost,
                                               textureRegistryStats) ||
            !validateViewportResizeSmoke(mode, *runResult, viewportStats, textureRegistryStats) ||
            !validateInputRouterSmoke(mode, *runResult) ||
            !validateShortcutRouterRunSmoke(mode, *runResult)) {
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
                  << ", live texture descriptors peak: " << textureRegistryStats.peakLiveDescriptors
                  << ", viewport: " << viewportExtent.width << 'x' << viewportExtent.height << '\n';
        if (isViewportResizeSmokeMode(mode)) {
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
