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
#include "editor_panel.hpp"
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
    constexpr int kSmokeAttemptLimit = 120;

    bool isSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode != asharia::editor::EditorRunMode::Interactive;
    }

    bool isViewportSmokeMode(asharia::editor::EditorRunMode mode) {
        return mode == asharia::editor::EditorRunMode::SmokeViewport;
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

    struct ImageBarrierDesc {
        VkImage image{VK_NULL_HANDLE};
        VkImageLayout oldLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImageLayout newLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkPipelineStageFlags2 srcStageMask{};
        VkAccessFlags2 srcAccessMask{};
        VkPipelineStageFlags2 dstStageMask{};
        VkAccessFlags2 dstAccessMask{};
    };

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

    [[nodiscard]] asharia::Result<int>
    runEditorLoop(asharia::GlfwWindow& window, asharia::VulkanFrameLoop& frameLoop,
                  asharia::BasicFullscreenTextureRenderer& renderer,
                  asharia::editor::EditorViewportCoordinator& viewportHost,
                  asharia::editor::EditorActionRegistry& actionRegistry,
                  asharia::editor::EditorContext& editorContext,
                  asharia::editor::EditorPanelRegistry& panelRegistry, bool smokeMode) {
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
            viewportHost.beginImguiFrame(asharia::editor::editorViewportFrameEpochs(frameLoop));
            panelRegistry.clearLifecycleEvents();
            editorContext.eventQueue().clear();
            asharia::editor::EditorFrameContext frameContext{
                .frameIndex = renderedFrames,
                .swapchainExtent = editorExtentFromVk(frameLoop.extent()),
                .smokeMode = smokeMode,
                .eventQueue = editorContext.eventQueue(),
                .diagnosticsLog = editorContext.diagnosticsLog(),
                .viewportHost = viewportHost,
            };
            buildEditorShell(actionRegistry, editorContext, panelRegistry, frameContext);
            ImGui::Render();

            auto rendered = renderEditorFrame(frameLoop, renderer, viewportHost);
            if (!rendered) {
                return std::unexpected{std::move(rendered.error())};
            }
            if (*rendered) {
                ++renderedFrames;
            }
            editorContext.diagnosticsLog().appendEvents(editorContext.eventQueue().events());
            editorContext.eventQueue().clear();
            panelRegistry.clearLifecycleEvents();

            if (smokeMode && renderedFrames >= kSmokeFrameCount) {
                break;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(smokeMode ? 16ms : 1ms);
        }

        return renderedFrames;
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
                .shortcut = "",
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
                .shortcut = "",
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

        auto renderedFrames =
            runEditorLoop(*window, *frameLoop, *renderer, viewportHost, actionRegistry,
                          editorContext, panelRegistry, smokeMode);
        if (!renderedFrames) {
            asharia::logError(renderedFrames.error().message);
            return EXIT_FAILURE;
        }
        if (isViewportSmokeMode(mode) && !viewportHost.hasPresentedViewportTexture()) {
            asharia::logError("Editor viewport smoke did not present a sampled viewport texture.");
            return EXIT_FAILURE;
        }
        if (isViewportSmokeMode(mode) && viewportHost.textureFramesSubmitted() + 1U <
                                             static_cast<std::uint64_t>(*renderedFrames)) {
            asharia::logError("Editor viewport smoke dropped sampled texture presentation during "
                              "resize.");
            return EXIT_FAILURE;
        }
        const asharia::editor::ImGuiTextureRegistryStats textureRegistryStats =
            viewportHost.textureRegistryStats();
        if (isViewportSmokeMode(mode) &&
            textureRegistryStats.peakLiveDescriptors >
                asharia::editor::kEditorViewportTextureDescriptorBudget) {
            asharia::logError("Editor viewport smoke exceeded ImGui texture descriptor budget.");
            return EXIT_FAILURE;
        }

        const VkExtent2D viewportExtent = viewportHost.descriptorExtent();
        viewportHost.shutdown();
        imgui.shutdown();
        window->requestClose();

        std::cout << "Editor shell frames: " << *renderedFrames
                  << ", viewport frames: " << viewportHost.viewportFramesRendered()
                  << ", texture frames: " << viewportHost.textureFramesSubmitted()
                  << ", live texture descriptors peak: " << textureRegistryStats.peakLiveDescriptors
                  << ", viewport: " << viewportExtent.width << 'x' << viewportExtent.height << '\n';
        return EXIT_SUCCESS;
    }

} // namespace asharia::editor
