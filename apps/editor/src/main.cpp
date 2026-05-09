#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "vke/core/log.hpp"
#include "vke/core/result.hpp"
#include "vke/core/version.hpp"
#include "vke/renderer_basic_vulkan/basic_triangle_renderer.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"
#include "vke/rhi_vulkan/vulkan_error.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan/vulkan_image.hpp"
#include "vke/window_glfw/glfw_window.hpp"

namespace {

    constexpr vke::VulkanDebugLabelMode kEditorDebugLabels = vke::VulkanDebugLabelMode::Required;
    constexpr int kSmokeFrameCount = 3;
    constexpr int kSmokeAttemptLimit = 120;

    bool hasArg(std::span<char*> args, std::string_view expected) {
        return std::ranges::any_of(
            args, [expected](const char* arg) { return arg != nullptr && arg == expected; });
    }

    void printVersion() {
        std::cout << vke::kEngineName << " editor " << vke::kEngineVersion.major << '.'
                  << vke::kEngineVersion.minor << '.' << vke::kEngineVersion.patch << '\n';
    }

    void printUsage() {
        std::cout << "Usage: vke-editor [--help] [--version] [--smoke-editor-shell] "
                     "[--smoke-editor-viewport]\n";
    }

    bool isRenderableExtent(vke::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, vke::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    bool sameExtent(VkExtent2D lhs, VkExtent2D rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    bool closeExtent(VkExtent2D lhs, VkExtent2D rhs) {
        constexpr std::uint32_t kExtentTolerance = 4;
        const std::uint32_t widthDelta =
            lhs.width > rhs.width ? lhs.width - rhs.width : rhs.width - lhs.width;
        const std::uint32_t heightDelta =
            lhs.height > rhs.height ? lhs.height - rhs.height : rhs.height - lhs.height;
        return widthDelta <= kExtentTolerance && heightDelta <= kExtentTolerance;
    }

    VkExtent2D viewportExtentFromAvailableSize(ImVec2 available) {
        return VkExtent2D{
            .width = std::max(1U, static_cast<std::uint32_t>(std::max(available.x, 1.0F))),
            .height = std::max(1U, static_cast<std::uint32_t>(std::max(available.y, 1.0F))),
        };
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

    void checkImguiVkResult(VkResult result) {
        if (result != VK_SUCCESS) {
            vke::logError("Dear ImGui Vulkan backend error: " + vke::vkResultName(result));
        }
    }

    class ImGuiRuntime {
    public:
        ImGuiRuntime() = default;
        ImGuiRuntime(const ImGuiRuntime&) = delete;
        ImGuiRuntime& operator=(const ImGuiRuntime&) = delete;
        ImGuiRuntime(ImGuiRuntime&&) = delete;
        ImGuiRuntime& operator=(ImGuiRuntime&&) = delete;

        ~ImGuiRuntime() {
            shutdown();
        }

        [[nodiscard]] vke::VoidResult create(GLFWwindow* window, const vke::VulkanContext& context,
                                             const vke::VulkanFrameLoop& frameLoop) {
            queue_ = context.graphicsQueue();

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            contextCreated_ = true;

            ImGuiIO& imguiIo = ImGui::GetIO();
            imguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            imguiIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            imguiIo.IniFilename = nullptr;

            if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
                return std::unexpected{
                    vke::vulkanError("Failed to initialize Dear ImGui GLFW backend")};
            }
            glfwInitialized_ = true;

            VkFormat colorFormat = frameLoop.format();
            VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
            pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            pipelineRenderingInfo.colorAttachmentCount = 1;
            pipelineRenderingInfo.pColorAttachmentFormats = &colorFormat;

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.ApiVersion = context.instanceApiVersion();
            initInfo.Instance = context.instance();
            initInfo.PhysicalDevice = context.physicalDevice();
            initInfo.Device = context.device();
            initInfo.QueueFamily = context.graphicsQueueFamily();
            initInfo.Queue = context.graphicsQueue();
            initInfo.DescriptorPoolSize = 128;
            initInfo.MinImageCount = 2;
            initInfo.ImageCount = frameLoop.swapchainImageCount();
            initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRenderingInfo;
            initInfo.UseDynamicRendering = true;
            initInfo.CheckVkResultFn = checkImguiVkResult;
            initInfo.MinAllocationSize = static_cast<VkDeviceSize>(1024ULL * 1024ULL);

            if (!ImGui_ImplVulkan_Init(&initInfo)) {
                return std::unexpected{
                    vke::vulkanError("Failed to initialize Dear ImGui Vulkan backend")};
            }
            vulkanInitialized_ = true;

            return {};
        }

        void shutdown() {
            if (vulkanInitialized_ && queue_ != VK_NULL_HANDLE) {
                const VkResult idleResult = vkQueueWaitIdle(queue_);
                if (idleResult != VK_SUCCESS) {
                    vke::logError("Failed to wait for Vulkan queue before ImGui shutdown: " +
                                  vke::vkResultName(idleResult));
                }
            }

            if (vulkanInitialized_) {
                ImGui_ImplVulkan_Shutdown();
                vulkanInitialized_ = false;
            }
            if (glfwInitialized_) {
                ImGui_ImplGlfw_Shutdown();
                glfwInitialized_ = false;
            }
            if (contextCreated_) {
                ImGui::DestroyContext();
                contextCreated_ = false;
            }

            queue_ = VK_NULL_HANDLE;
        }

    private:
        VkQueue queue_{VK_NULL_HANDLE};
        bool contextCreated_{false};
        bool glfwInitialized_{false};
        bool vulkanInitialized_{false};
    };

    class EditorViewportHost {
    public:
        EditorViewportHost() = default;
        EditorViewportHost(const EditorViewportHost&) = delete;
        EditorViewportHost& operator=(const EditorViewportHost&) = delete;
        EditorViewportHost(EditorViewportHost&&) = delete;
        EditorViewportHost& operator=(EditorViewportHost&&) = delete;

        ~EditorViewportHost() {
            shutdown();
        }

        [[nodiscard]] vke::VoidResult create(const vke::VulkanContext& context) {
            device_ = context.device();
            allocator_ = context.allocator();
            queue_ = context.graphicsQueue();

            auto sampler = vke::VulkanSampler::create(vke::VulkanSamplerDesc{
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
        }

        void requestViewport(VkExtent2D extent, VkFormat format) {
            requestedExtent_ = extent;
            requestedFormat_ = format;
        }

        [[nodiscard]] bool canDrawRequestedTexture() const {
            return descriptorSet_ != VK_NULL_HANDLE &&
                   closeExtent(descriptorExtent_, requestedExtent_) &&
                   descriptorFormat_ == requestedFormat_;
        }

        void drawRequestedTexture() {
            if (!canDrawRequestedTexture()) {
                return;
            }

            textureSubmittedThisFrame_ = true;
            ImGui::Image(imguiTextureId(descriptorSet_),
                         ImVec2{static_cast<float>(descriptorExtent_.width),
                                static_cast<float>(descriptorExtent_.height)});
            ++textureFramesSubmitted_;
        }

        [[nodiscard]] vke::Result<vke::VulkanFrameRecordResult>
        recordViewport(const vke::VulkanFrameRecordContext& frame,
                       vke::BasicFullscreenTextureRenderer& renderer) {
            if (requestedExtent_.width == 0 || requestedExtent_.height == 0 ||
                requestedFormat_ == VK_FORMAT_UNDEFINED) {
                return vke::VulkanFrameRecordResult{
                    .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                };
            }

            VkExtent2D renderExtent = requestedExtent_;
            VkFormat renderFormat = requestedFormat_;
            if (textureSubmittedThisFrame_ && descriptorSet_ != VK_NULL_HANDLE) {
                renderExtent = descriptorExtent_;
                renderFormat = descriptorFormat_;
            }

            auto ensured =
                renderTarget_.ensure(frame, vke::VulkanRenderTargetDesc{
                                                .device = device_,
                                                .allocator = allocator_,
                                                .format = renderFormat,
                                                .extent = renderExtent,
                                                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                         VK_IMAGE_USAGE_SAMPLED_BIT,
                                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                            });
            if (!ensured) {
                return std::unexpected{std::move(ensured.error())};
            }

            const vke::VulkanSampledTextureView texture = renderTarget_.sampledTextureView();
            auto descriptorReady = ensureDescriptor(texture);
            if (!descriptorReady) {
                return std::unexpected{std::move(descriptorReady.error())};
            }

            auto recorded = renderer.recordViewFrame(
                frame,
                vke::BasicRenderViewDesc{
                    .target =
                        vke::BasicRenderViewTarget{
                            .image = texture.image,
                            .imageView = texture.imageView,
                            .format = texture.format,
                            .extent = texture.extent,
                            .aspectMask = texture.aspectMask,
                            .finalUsage = vke::BasicRenderViewTargetFinalUsage::SampledTexture,
                        },
                });
            if (!recorded) {
                return std::unexpected{std::move(recorded.error())};
            }

            ++viewportFramesRendered_;
            return *recorded;
        }

        void shutdown() {
            if (descriptorSet_ == VK_NULL_HANDLE) {
                return;
            }

            if (queue_ != VK_NULL_HANDLE) {
                const VkResult idleResult = vkQueueWaitIdle(queue_);
                if (idleResult != VK_SUCCESS) {
                    vke::logError("Failed to wait for Vulkan queue before editor viewport "
                                  "texture shutdown: " +
                                  vke::vkResultName(idleResult));
                }
            }
            ImGui_ImplVulkan_RemoveTexture(descriptorSet_);
            descriptorSet_ = VK_NULL_HANDLE;
            descriptorImageView_ = VK_NULL_HANDLE;
            descriptorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
            descriptorFormat_ = VK_FORMAT_UNDEFINED;
            descriptorExtent_ = {};
        }

        [[nodiscard]] bool hasPresentedViewportTexture() const {
            return viewportFramesRendered_ > 0 && textureFramesSubmitted_ > 0;
        }

        [[nodiscard]] VkExtent2D descriptorExtent() const {
            return descriptorExtent_;
        }

        [[nodiscard]] std::uint64_t viewportFramesRendered() const {
            return viewportFramesRendered_;
        }

    private:
        [[nodiscard]] vke::VoidResult ensureDescriptor(vke::VulkanSampledTextureView texture) {
            if (texture.imageView == VK_NULL_HANDLE ||
                texture.sampledLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
                texture.format == VK_FORMAT_UNDEFINED || texture.extent.width == 0 ||
                texture.extent.height == 0) {
                return std::unexpected{
                    vke::vulkanError("Cannot register an incomplete editor viewport texture")};
            }

            if (descriptorSet_ != VK_NULL_HANDLE && descriptorImageView_ == texture.imageView &&
                descriptorLayout_ == texture.sampledLayout && descriptorFormat_ == texture.format &&
                sameExtent(descriptorExtent_, texture.extent)) {
                return {};
            }

            if (textureSubmittedThisFrame_) {
                return std::unexpected{vke::vulkanError(
                    "Cannot replace an editor viewport texture after submitting it to ImGui")};
            }

            if (descriptorSet_ != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(descriptorSet_);
            }

            descriptorSet_ = ImGui_ImplVulkan_AddTexture(sampler_.handle(), texture.imageView,
                                                         texture.sampledLayout);
            if (descriptorSet_ == VK_NULL_HANDLE) {
                descriptorImageView_ = VK_NULL_HANDLE;
                descriptorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
                descriptorFormat_ = VK_FORMAT_UNDEFINED;
                descriptorExtent_ = {};
                return std::unexpected{
                    vke::vulkanError("Failed to register editor viewport texture with ImGui")};
            }

            descriptorImageView_ = texture.imageView;
            descriptorLayout_ = texture.sampledLayout;
            descriptorFormat_ = texture.format;
            descriptorExtent_ = texture.extent;
            return {};
        }

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VkQueue queue_{VK_NULL_HANDLE};
        vke::VulkanSampler sampler_;
        vke::VulkanRenderTarget renderTarget_;
        VkExtent2D requestedExtent_{};
        VkFormat requestedFormat_{VK_FORMAT_UNDEFINED};
        VkDescriptorSet descriptorSet_{VK_NULL_HANDLE};
        VkImageView descriptorImageView_{VK_NULL_HANDLE};
        VkImageLayout descriptorLayout_{VK_IMAGE_LAYOUT_UNDEFINED};
        VkFormat descriptorFormat_{VK_FORMAT_UNDEFINED};
        VkExtent2D descriptorExtent_{};
        bool textureSubmittedThisFrame_{false};
        std::uint64_t viewportFramesRendered_{0};
        std::uint64_t textureFramesSubmitted_{0};
    };

    void buildEditorShell(int frameIndex, VkExtent2D extent, VkFormat format, bool smokeMode,
                          EditorViewportHost& viewportHost) {
        ImGui::DockSpaceOverViewport();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                ImGui::MenuItem("New Scene", nullptr, false, false);
                ImGui::MenuItem("Open...", nullptr, false, false);
                ImGui::Separator();
                ImGui::MenuItem("Exit", nullptr, false, false);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Scene View", nullptr, true, false);
                ImGui::MenuItem("Log", nullptr, true, false);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        const ImGuiCond sceneWindowCond = smokeMode ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        if (const ImGuiViewport* mainViewport = ImGui::GetMainViewport(); mainViewport != nullptr) {
            ImGui::SetNextWindowPos(
                ImVec2{mainViewport->WorkPos.x + 8.0F, mainViewport->WorkPos.y + 8.0F},
                sceneWindowCond);
        }
        ImGui::SetNextWindowSize(
            ImVec2{std::max(320.0F, static_cast<float>(extent.width) * 0.66F),
                   std::max(240.0F, static_cast<float>(extent.height) * 0.72F)},
            sceneWindowCond);
        ImGui::Begin("Scene View");
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        viewportSize.y =
            std::max(1.0F, viewportSize.y - (ImGui::GetTextLineHeightWithSpacing() * 3.0F));
        const VkExtent2D viewportExtent = viewportExtentFromAvailableSize(viewportSize);
        viewportHost.requestViewport(viewportExtent, format);
        if (viewportHost.canDrawRequestedTexture()) {
            viewportHost.drawRequestedTexture();
        } else {
            ImGui::Dummy(ImVec2{static_cast<float>(viewportExtent.width),
                                static_cast<float>(viewportExtent.height)});
        }

        const std::string swapchainText =
            "Swapchain: " + std::to_string(extent.width) + "x" + std::to_string(extent.height);
        const std::string viewportText = "Viewport: " + std::to_string(viewportExtent.width) + "x" +
                                         std::to_string(viewportExtent.height);
        const std::string frameText = "Frame: " + std::to_string(frameIndex);
        ImGui::TextUnformatted(swapchainText.c_str());
        ImGui::TextUnformatted(viewportText.c_str());
        ImGui::TextUnformatted(frameText.c_str());
        ImGui::End();

        ImGui::Begin("Log");
        const std::string modeText = std::string{"Mode: "} + (smokeMode ? "smoke" : "interactive");
        ImGui::TextUnformatted("Editor shell initialized with GLFW + Vulkan + Dear ImGui.");
        ImGui::TextUnformatted(modeText.c_str());
        ImGui::End();
    }

    [[nodiscard]] vke::Result<vke::VulkanFrameRecordResult>
    recordEditorImguiFrame(const vke::VulkanFrameRecordContext& context) {
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

        return vke::VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    }

    [[nodiscard]] vke::VoidResult waitForRenderableWindow(vke::GlfwWindow& window, bool smokeMode) {
        int attempts = 0;
        auto framebuffer = window.framebufferExtent();
        while (!window.shouldClose() && !isRenderableExtent(framebuffer)) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{
                    vke::vulkanError("Timed out waiting for a renderable editor framebuffer")};
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(16ms);
            vke::GlfwWindow::pollEvents();
            framebuffer = window.framebufferExtent();
        }

        return {};
    }

    [[nodiscard]] vke::Result<vke::VulkanContext>
    createEditorContext(const std::vector<std::string>& extensions, vke::GlfwWindow& window) {
        const vke::VulkanContextDesc contextDesc{
            .applicationName = "VkEngine Editor",
            .requiredInstanceExtensions = extensions,
            .createSurface =
                [&window](VkInstance instance) {
                    return vke::glfwCreateVulkanSurface(window, instance);
                },
            .debugLabels = kEditorDebugLabels,
        };

        return vke::VulkanContext::create(contextDesc);
    }

    [[nodiscard]] vke::Result<vke::VulkanFrameLoop>
    createEditorFrameLoop(const vke::VulkanContext& context, const vke::GlfwWindow& window) {
        const auto framebuffer = window.framebufferExtent();
        return vke::VulkanFrameLoop::create(
            context, vke::VulkanFrameLoopDesc{
                         .width = framebuffer.width,
                         .height = framebuffer.height,
                         .clearColor = VkClearColorValue{{0.015F, 0.018F, 0.022F, 1.0F}},
                     });
    }

    [[nodiscard]] vke::Result<bool> prepareFrameLoopExtent(vke::GlfwWindow& window,
                                                           vke::VulkanFrameLoop& frameLoop) {
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
            if (*recreated == vke::VulkanFrameStatus::OutOfDate) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(16ms);
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] vke::Result<bool> renderEditorFrame(vke::VulkanFrameLoop& frameLoop,
                                                      vke::BasicFullscreenTextureRenderer& renderer,
                                                      EditorViewportHost& viewportHost) {
        auto status = frameLoop.renderFrame(
            [&renderer, &viewportHost](const vke::VulkanFrameRecordContext& context)
                -> vke::Result<vke::VulkanFrameRecordResult> {
                auto viewport = viewportHost.recordViewport(context, renderer);
                if (!viewport) {
                    return std::unexpected{std::move(viewport.error())};
                }

                return recordEditorImguiFrame(context);
            });
        if (!status) {
            return std::unexpected{std::move(status.error())};
        }

        return *status != vke::VulkanFrameStatus::OutOfDate;
    }

    [[nodiscard]] vke::Result<int> runEditorLoop(vke::GlfwWindow& window,
                                                 vke::VulkanFrameLoop& frameLoop,
                                                 vke::BasicFullscreenTextureRenderer& renderer,
                                                 EditorViewportHost& viewportHost, bool smokeMode) {
        int renderedFrames = 0;
        int attempts = 0;
        while (!window.shouldClose()) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{vke::vulkanError(
                    "Editor shell smoke timed out before rendering enough frames")};
            }

            vke::GlfwWindow::pollEvents();
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
            buildEditorShell(renderedFrames, frameLoop.extent(), frameLoop.format(), smokeMode,
                             viewportHost);
            ImGui::Render();

            auto rendered = renderEditorFrame(frameLoop, renderer, viewportHost);
            if (!rendered) {
                return std::unexpected{std::move(rendered.error())};
            }
            if (*rendered) {
                ++renderedFrames;
            }

            if (smokeMode && renderedFrames >= kSmokeFrameCount) {
                break;
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(smokeMode ? 16ms : 1ms);
        }

        return renderedFrames;
    }

    int runEditor(bool smokeMode) {
        auto glfw = vke::GlfwInstance::create();
        if (!glfw) {
            vke::logError(glfw.error().message);
            return EXIT_FAILURE;
        }

        auto extensions = vke::glfwRequiredVulkanInstanceExtensions(*glfw);
        if (!extensions) {
            vke::logError(extensions.error().message);
            return EXIT_FAILURE;
        }

        auto window = vke::GlfwWindow::create(*glfw, vke::WindowDesc{.title = "VkEngine Editor"});
        if (!window) {
            vke::logError(window.error().message);
            return EXIT_FAILURE;
        }

        auto context = createEditorContext(*extensions, *window);
        if (!context) {
            vke::logError(context.error().message);
            return EXIT_FAILURE;
        }

        vke::GlfwWindow::pollEvents();
        if (auto waited = waitForRenderableWindow(*window, smokeMode); !waited) {
            vke::logError(waited.error().message);
            return EXIT_FAILURE;
        }
        if (window->shouldClose()) {
            return EXIT_SUCCESS;
        }

        auto frameLoop = createEditorFrameLoop(*context, *window);
        if (!frameLoop) {
            vke::logError(frameLoop.error().message);
            return EXIT_FAILURE;
        }

        ImGuiRuntime imgui;
        if (auto created = imgui.create(window->nativeHandle(), *context, *frameLoop); !created) {
            vke::logError(created.error().message);
            return EXIT_FAILURE;
        }

        const std::filesystem::path shaderDir{VKE_RENDERER_BASIC_SHADER_OUTPUT_DIR};
        auto renderer =
            vke::BasicFullscreenTextureRenderer::create(vke::BasicFullscreenTextureRendererDesc{
                .device = context->device(),
                .allocator = context->allocator(),
                .shaderDirectory = shaderDir,
            });
        if (!renderer) {
            vke::logError(renderer.error().message);
            return EXIT_FAILURE;
        }

        EditorViewportHost viewportHost;
        if (auto created = viewportHost.create(*context); !created) {
            vke::logError(created.error().message);
            return EXIT_FAILURE;
        }

        auto renderedFrames =
            runEditorLoop(*window, *frameLoop, *renderer, viewportHost, smokeMode);
        if (!renderedFrames) {
            vke::logError(renderedFrames.error().message);
            return EXIT_FAILURE;
        }
        if (smokeMode && !viewportHost.hasPresentedViewportTexture()) {
            vke::logError("Editor viewport smoke did not present a sampled viewport texture.");
            return EXIT_FAILURE;
        }

        const VkExtent2D viewportExtent = viewportHost.descriptorExtent();
        viewportHost.shutdown();
        imgui.shutdown();
        window->requestClose();

        std::cout << "Editor shell frames: " << *renderedFrames
                  << ", viewport frames: " << viewportHost.viewportFramesRendered()
                  << ", viewport: " << viewportExtent.width << 'x' << viewportExtent.height << '\n';
        return EXIT_SUCCESS;
    }

} // namespace

int main(int argc, char** argv) {
    try {
        std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        if (hasArg(args, "--help")) {
            printUsage();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--version")) {
            printVersion();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--smoke-editor-shell") || hasArg(args, "--smoke-editor-viewport")) {
            return runEditor(true);
        }

        if (args.size() == 1) {
            return runEditor(false);
        }

        printVersion();
        printUsage();
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        vke::logError(exception.what());
    } catch (...) {
        vke::logError("Unhandled non-standard exception.");
    }

    return EXIT_FAILURE;
}
