#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "vke/core/log.hpp"
#include "vke/core/result.hpp"
#include "vke/core/version.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"
#include "vke/rhi_vulkan/vulkan_error.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/window_glfw/glfw_window.hpp"

namespace {

    constexpr vke::VulkanDebugLabelMode kEditorDebugLabels =
        vke::VulkanDebugLabelMode::Required;
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
        std::cout << "Usage: vke-editor [--help] [--version] [--smoke-editor-shell]\n";
    }

    bool isRenderableExtent(vke::WindowFramebufferExtent extent) {
        return extent.width > 0 && extent.height > 0;
    }

    bool extentMatches(VkExtent2D lhs, vke::WindowFramebufferExtent rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height;
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

    void buildEditorShell(int frameIndex, VkExtent2D extent, bool smokeMode) {
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

        ImGui::Begin("Scene View");
        const std::string swapchainText = "Swapchain: " + std::to_string(extent.width) + "x" +
                                          std::to_string(extent.height);
        const std::string frameText = "Frame: " + std::to_string(frameIndex);
        ImGui::TextUnformatted("RenderView placeholder");
        ImGui::TextUnformatted(swapchainText.c_str());
        ImGui::TextUnformatted(frameText.c_str());
        ImGui::End();

        ImGui::Begin("Log");
        const std::string modeText =
            std::string{"Mode: "} + (smokeMode ? "smoke" : "interactive");
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

    [[nodiscard]] vke::VoidResult waitForRenderableWindow(vke::GlfwWindow& window,
                                                          bool smokeMode) {
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

    [[nodiscard]] vke::Result<bool>
    prepareFrameLoopExtent(vke::GlfwWindow& window, vke::VulkanFrameLoop& frameLoop) {
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

    [[nodiscard]] vke::Result<bool> renderEditorFrame(vke::VulkanFrameLoop& frameLoop) {
        auto status = frameLoop.renderFrame(recordEditorImguiFrame);
        if (!status) {
            return std::unexpected{std::move(status.error())};
        }

        return *status != vke::VulkanFrameStatus::OutOfDate;
    }

    [[nodiscard]] vke::Result<int> runEditorLoop(vke::GlfwWindow& window,
                                                 vke::VulkanFrameLoop& frameLoop,
                                                 bool smokeMode) {
        int renderedFrames = 0;
        int attempts = 0;
        while (!window.shouldClose()) {
            if (smokeMode && attempts++ >= kSmokeAttemptLimit) {
                return std::unexpected{
                    vke::vulkanError("Editor shell smoke timed out before rendering enough frames")};
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
            buildEditorShell(renderedFrames, frameLoop.extent(), smokeMode);
            ImGui::Render();

            auto rendered = renderEditorFrame(frameLoop);
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

        auto window = vke::GlfwWindow::create(
            *glfw, vke::WindowDesc{.title = "VkEngine Editor"});
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

        auto renderedFrames = runEditorLoop(*window, *frameLoop, smokeMode);
        if (!renderedFrames) {
            vke::logError(renderedFrames.error().message);
            return EXIT_FAILURE;
        }

        imgui.shutdown();
        window->requestClose();

        std::cout << "Editor shell frames: " << *renderedFrames << '\n';
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

        if (hasArg(args, "--smoke-editor-shell")) {
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
