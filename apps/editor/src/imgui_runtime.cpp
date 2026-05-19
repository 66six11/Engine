#include "imgui_runtime.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace {

    void checkImguiVkResult(VkResult result) {
        if (result != VK_SUCCESS) {
            asharia::logError("Dear ImGui Vulkan backend error: " + asharia::vkResultName(result));
        }
    }

} // namespace

namespace asharia::editor {

    ImGuiRuntime::~ImGuiRuntime() {
        shutdown();
    }

    asharia::VoidResult ImGuiRuntime::create(GLFWwindow* window,
                                             const asharia::VulkanContext& context,
                                             const asharia::VulkanFrameLoop& frameLoop) {
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
                asharia::vulkanError("Failed to initialize Dear ImGui GLFW backend")};
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
                asharia::vulkanError("Failed to initialize Dear ImGui Vulkan backend")};
        }
        vulkanInitialized_ = true;

        return {};
    }

    void ImGuiRuntime::shutdown() {
        if (vulkanInitialized_ && queue_ != VK_NULL_HANDLE) {
            const VkResult idleResult = vkQueueWaitIdle(queue_);
            if (idleResult != VK_SUCCESS) {
                asharia::logError("Failed to wait for Vulkan queue before ImGui shutdown: " +
                                  asharia::vkResultName(idleResult));
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

} // namespace asharia::editor
