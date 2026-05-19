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
#include "asharia/rhi_vulkan/vulkan_image.hpp"
#include "asharia/window_glfw/glfw_window.hpp"

#include "editor_action.hpp"
#include "editor_context.hpp"
#include "editor_panel.hpp"
#include "imgui_editor_shell.hpp"
#include "imgui_runtime.hpp"
#include "panels/log_panel.hpp"
#include "panels/scene_view_panel.hpp"

namespace {

    constexpr asharia::VulkanDebugLabelMode kEditorDebugLabels =
        asharia::VulkanDebugLabelMode::Required;
    constexpr int kSmokeFrameCount = 3;
    constexpr int kSmokeAttemptLimit = 120;

    bool isRenderableExtent(asharia::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, asharia::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    bool sameExtent(VkExtent2D lhs, VkExtent2D rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    ImTextureID imguiTextureId(VkDescriptorSet descriptorSet) {
        return static_cast<ImTextureID>(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<std::uintptr_t>(descriptorSet));
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

    class EditorViewportHost final : public asharia::editor::EditorViewportPanelHost {
        struct ViewportTexture {
            ViewportTexture() = default;
            ViewportTexture(const ViewportTexture&) = delete;
            ViewportTexture& operator=(const ViewportTexture&) = delete;
            ViewportTexture(ViewportTexture&&) noexcept = default;
            ViewportTexture& operator=(ViewportTexture&&) noexcept = default;
            ~ViewportTexture() = default;

            [[nodiscard]] bool hasDescriptor() const {
                return descriptorSet != VK_NULL_HANDLE;
            }

            [[nodiscard]] bool ready() const {
                return hasDescriptor() && rendered;
            }

            void clearDescriptorMetadata() {
                descriptorSet = VK_NULL_HANDLE;
                imageView = VK_NULL_HANDLE;
                layout = VK_IMAGE_LAYOUT_UNDEFINED;
                format = VK_FORMAT_UNDEFINED;
                extent = {};
                rendered = false;
            }

            asharia::VulkanRenderTarget target;
            VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
            VkImageView imageView{VK_NULL_HANDLE};
            VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
            VkFormat format{VK_FORMAT_UNDEFINED};
            VkExtent2D extent{};
            bool rendered{false};
        };

    public:
        EditorViewportHost() = default;
        EditorViewportHost(const EditorViewportHost&) = delete;
        EditorViewportHost& operator=(const EditorViewportHost&) = delete;
        EditorViewportHost(EditorViewportHost&&) = delete;
        EditorViewportHost& operator=(EditorViewportHost&&) = delete;

        ~EditorViewportHost() override {
            shutdown();
        }

        [[nodiscard]] asharia::VoidResult create(const asharia::VulkanContext& context) {
            device_ = context.device();
            allocator_ = context.allocator();
            queue_ = context.graphicsQueue();

            auto sampler = asharia::VulkanSampler::create(asharia::VulkanSamplerDesc{
                .device = device_,
            });
            if (!sampler) {
                return std::unexpected{std::move(sampler.error())};
            }
            sampler_ = std::move(*sampler);
            return {};
        }

        void beginImguiFrame() {
            textureSubmittedThisFrame_ = false;
            promotePendingTexture();
        }

        void requestViewport(VkExtent2D extent, VkFormat format) override {
            requestedExtent_ = extent;
            requestedFormat_ = format;
        }

        [[nodiscard]] bool canDrawRequestedTexture() const override {
            return presentedTexture_.ready();
        }

        void drawRequestedTexture() override {
            if (!canDrawRequestedTexture()) {
                return;
            }

            const VkExtent2D displayExtent =
                requestedExtent_.width == 0 || requestedExtent_.height == 0
                    ? presentedTexture_.extent
                    : requestedExtent_;
            textureSubmittedThisFrame_ = true;
            ImGui::Image(imguiTextureId(presentedTexture_.descriptorSet),
                         ImVec2{static_cast<float>(displayExtent.width),
                                static_cast<float>(displayExtent.height)});
            ++textureFramesSubmitted_;
        }

        [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
        recordViewport(const asharia::VulkanFrameRecordContext& frame,
                       asharia::BasicFullscreenTextureRenderer& renderer) {
            auto retired = processRetiredTextures(frame);
            if (!retired) {
                return std::unexpected{std::move(retired.error())};
            }

            if (requestedExtent_.width == 0 || requestedExtent_.height == 0 ||
                requestedFormat_ == VK_FORMAT_UNDEFINED) {
                return asharia::VulkanFrameRecordResult{
                    .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                };
            }

            const bool renderPresentedTexture =
                presentedTexture_.ready() &&
                sameExtent(presentedTexture_.extent, requestedExtent_) &&
                presentedTexture_.format == requestedFormat_;
            ViewportTexture& renderTexture =
                renderPresentedTexture ? presentedTexture_ : pendingTexture_;

            auto ensured = renderTexture.target.ensure(
                frame,
                asharia::VulkanRenderTargetDesc{
                    .device = device_,
                    .allocator = allocator_,
                    .format = requestedFormat_,
                    .extent = requestedExtent_,
                    .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                });
            if (!ensured) {
                return std::unexpected{std::move(ensured.error())};
            }

            const asharia::VulkanSampledTextureView texture =
                renderTexture.target.sampledTextureView();
            auto descriptorReady = ensureDescriptor(renderTexture, texture);
            if (!descriptorReady) {
                return std::unexpected{std::move(descriptorReady.error())};
            }

            auto recorded = renderer.recordViewFrame(
                frame,
                asharia::BasicRenderViewDesc{
                    .target =
                        asharia::BasicRenderViewTarget{
                            .image = texture.image,
                            .imageView = texture.imageView,
                            .format = texture.format,
                            .extent = texture.extent,
                            .aspectMask = texture.aspectMask,
                            .finalUsage = asharia::BasicRenderViewTargetFinalUsage::SampledTexture,
                        },
                });
            if (!recorded) {
                return std::unexpected{std::move(recorded.error())};
            }

            renderTexture.rendered = true;
            ++viewportFramesRendered_;
            return *recorded;
        }

        void shutdown() {
            if (!hasTextureToRelease()) {
                return;
            }

            if (queue_ != VK_NULL_HANDLE) {
                const VkResult idleResult = vkQueueWaitIdle(queue_);
                if (idleResult != VK_SUCCESS) {
                    asharia::logError("Failed to wait for Vulkan queue before editor viewport "
                                      "texture shutdown: " +
                                      asharia::vkResultName(idleResult));
                }
            }
            removeDescriptor(presentedTexture_);
            removeDescriptor(pendingTexture_);
            for (ViewportTexture& texture : retiredTextures_) {
                removeDescriptor(texture);
            }
            presentedTexture_ = {};
            pendingTexture_ = {};
            retiredTextures_.clear();
        }

        [[nodiscard]] bool hasPresentedViewportTexture() const {
            return viewportFramesRendered_ > 0 && textureFramesSubmitted_ > 0;
        }

        [[nodiscard]] VkExtent2D descriptorExtent() const {
            if (presentedTexture_.ready()) {
                return presentedTexture_.extent;
            }
            return pendingTexture_.extent;
        }

        [[nodiscard]] std::uint64_t viewportFramesRendered() const {
            return viewportFramesRendered_;
        }

        [[nodiscard]] std::uint64_t textureFramesSubmitted() const {
            return textureFramesSubmitted_;
        }

    private:
        void promotePendingTexture() {
            if (!pendingTexture_.ready()) {
                return;
            }
            if (presentedTexture_.hasDescriptor()) {
                retiredTextures_.push_back(std::move(presentedTexture_));
            }
            presentedTexture_ = std::move(pendingTexture_);
            pendingTexture_ = {};
        }

        [[nodiscard]] bool hasTextureToRelease() const {
            return presentedTexture_.hasDescriptor() || pendingTexture_.hasDescriptor() ||
                   std::ranges::any_of(retiredTextures_, [](const ViewportTexture& texture) {
                       return texture.hasDescriptor();
                   });
        }

        static void removeDescriptor(ViewportTexture& texture) {
            if (texture.descriptorSet != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(texture.descriptorSet);
            }
            texture.clearDescriptorMetadata();
        }

        [[nodiscard]] asharia::VoidResult
        processRetiredTextures(const asharia::VulkanFrameRecordContext& frame) {
            for (ViewportTexture& texture : retiredTextures_) {
                removeDescriptor(texture);
                if (!texture.target.deferDestroy(frame)) {
                    return std::unexpected{
                        asharia::vulkanError("Failed to defer retired editor viewport texture")};
                }
            }
            retiredTextures_.clear();
            return {};
        }

        [[nodiscard]] asharia::VoidResult
        ensureDescriptor(ViewportTexture& viewportTexture,
                         asharia::VulkanSampledTextureView texture) {
            if (texture.imageView == VK_NULL_HANDLE ||
                texture.sampledLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
                texture.format == VK_FORMAT_UNDEFINED || texture.extent.width == 0 ||
                texture.extent.height == 0) {
                return std::unexpected{
                    asharia::vulkanError("Cannot register an incomplete editor viewport texture")};
            }

            if (viewportTexture.descriptorSet != VK_NULL_HANDLE &&
                viewportTexture.imageView == texture.imageView &&
                viewportTexture.layout == texture.sampledLayout &&
                viewportTexture.format == texture.format &&
                sameExtent(viewportTexture.extent, texture.extent)) {
                return {};
            }

            if (&viewportTexture == &presentedTexture_ && textureSubmittedThisFrame_) {
                return std::unexpected{asharia::vulkanError(
                    "Cannot replace an editor viewport texture after submitting it to ImGui")};
            }

            if (viewportTexture.descriptorSet != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(viewportTexture.descriptorSet);
            }

            viewportTexture.descriptorSet = ImGui_ImplVulkan_AddTexture(
                sampler_.handle(), texture.imageView, texture.sampledLayout);
            if (viewportTexture.descriptorSet == VK_NULL_HANDLE) {
                viewportTexture.clearDescriptorMetadata();
                return std::unexpected{
                    asharia::vulkanError("Failed to register editor viewport texture with ImGui")};
            }

            viewportTexture.imageView = texture.imageView;
            viewportTexture.layout = texture.sampledLayout;
            viewportTexture.format = texture.format;
            viewportTexture.extent = texture.extent;
            viewportTexture.rendered = false;
            return {};
        }

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VkQueue queue_{VK_NULL_HANDLE};
        asharia::VulkanSampler sampler_;
        ViewportTexture presentedTexture_;
        ViewportTexture pendingTexture_;
        std::vector<ViewportTexture> retiredTextures_;
        VkExtent2D requestedExtent_{};
        VkFormat requestedFormat_{VK_FORMAT_UNDEFINED};
        bool textureSubmittedThisFrame_{false};
        std::uint64_t viewportFramesRendered_{0};
        std::uint64_t textureFramesSubmitted_{0};
    };

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
                      EditorViewportHost& viewportHost) {
        auto status = frameLoop.renderFrame(
            [&renderer, &viewportHost](const asharia::VulkanFrameRecordContext& context)
                -> asharia::Result<asharia::VulkanFrameRecordResult> {
                auto viewport = viewportHost.recordViewport(context, renderer);
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
                  EditorViewportHost& viewportHost,
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
            viewportHost.beginImguiFrame();
            panelRegistry.clearLifecycleEvents();
            editorContext.eventQueue().clear();
            asharia::editor::EditorFrameContext frameContext{
                .frameIndex = renderedFrames,
                .swapchainExtent = frameLoop.extent(),
                .swapchainFormat = frameLoop.format(),
                .smokeMode = smokeMode,
                .eventQueue = editorContext.eventQueue(),
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
                return event.kind == asharia::editor::EditorEventKind::MenuActionInvoked &&
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

    int runEditor(bool smokeMode) {
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

        EditorViewportHost viewportHost;
        if (auto created = viewportHost.create(*context); !created) {
            asharia::logError(created.error().message);
            return EXIT_FAILURE;
        }

        asharia::editor::EditorEventQueue eventQueue;
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

        asharia::editor::EditorContext editorContext{panelRegistry, eventQueue};
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
        if (smokeMode && !viewportHost.hasPresentedViewportTexture()) {
            asharia::logError("Editor viewport smoke did not present a sampled viewport texture.");
            return EXIT_FAILURE;
        }
        if (smokeMode && viewportHost.textureFramesSubmitted() + 1U <
                             static_cast<std::uint64_t>(*renderedFrames)) {
            asharia::logError("Editor viewport smoke dropped sampled texture presentation during "
                              "resize.");
            return EXIT_FAILURE;
        }

        const VkExtent2D viewportExtent = viewportHost.descriptorExtent();
        viewportHost.shutdown();
        imgui.shutdown();
        window->requestClose();

        std::cout << "Editor shell frames: " << *renderedFrames
                  << ", viewport frames: " << viewportHost.viewportFramesRendered()
                  << ", texture frames: " << viewportHost.textureFramesSubmitted()
                  << ", viewport: " << viewportExtent.width << 'x' << viewportExtent.height << '\n';
        return EXIT_SUCCESS;
    }

} // namespace asharia::editor
