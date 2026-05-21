#include "imgui_runtime.hpp"

#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <memory>
#include <string>
#include <system_error>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

#include "editor_ui.hpp"

namespace {

    void checkImguiVkResult(VkResult result) {
        if (result != VK_SUCCESS) {
            asharia::logError("Dear ImGui Vulkan backend error: " + asharia::vkResultName(result));
        }
    }

    [[nodiscard]] std::string localAppDataPath() {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t valueSize = 0;
        if (_dupenv_s(&value, &valueSize, "LOCALAPPDATA") != 0 || value == nullptr) {
            return {};
        }
        const std::unique_ptr<char, decltype(&std::free)> ownedValue{value, &std::free};
        std::string result{ownedValue.get()};
        return result;
#else
        if (const char* value = std::getenv("LOCALAPPDATA"); value != nullptr && value[0] != '\0') {
            return value;
        }
        return {};
#endif
    }

    [[nodiscard]] std::filesystem::path editorLayoutIniPath() {
        std::filesystem::path basePath;
        const std::string localAppData = localAppDataPath();
        if (!localAppData.empty()) {
            basePath = localAppData;
        } else {
            std::error_code error;
            basePath = std::filesystem::temp_directory_path(error);
            if (error) {
                basePath = std::filesystem::current_path(error);
            }
            if (basePath.empty()) {
                basePath = ".";
            }
        }

        return basePath / "Asharia" / "Editor" / "imgui-layout.ini";
    }

    [[nodiscard]] std::string pathToUtf8String(const std::filesystem::path& path) {
        const std::u8string utf8 = path.u8string();
        std::string text;
        text.reserve(utf8.size());
        for (const char8_t character : utf8) {
            text.push_back(static_cast<char>(character));
        }
        return text;
    }

} // namespace

namespace asharia::editor {

    ImGuiRuntime::~ImGuiRuntime() {
        shutdown();
    }

    asharia::VoidResult ImGuiRuntime::create(GLFWwindow* window,
                                             const asharia::VulkanContext& context,
                                             const asharia::VulkanFrameLoop& frameLoop,
                                             const ImGuiRuntimeDesc& desc) {
        queue_ = context.graphicsQueue();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        contextCreated_ = true;

        ImGuiIO& imguiIo = ImGui::GetIO();
        imguiIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        imguiIo.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        applyEditorUiTheme();

        layoutIniPath_ = desc.layoutIniPath.empty() ? editorLayoutIniPath() : desc.layoutIniPath;
        std::error_code directoryError;
        std::filesystem::create_directories(layoutIniPath_.parent_path(), directoryError);
        if (!directoryError) {
            layoutIniPathUtf8_ = pathToUtf8String(layoutIniPath_);
            imguiIo.IniFilename = layoutIniPathUtf8_.c_str();
            layoutPersistenceEnabled_ = true;
        } else {
            imguiIo.IniFilename = nullptr;
            layoutPersistenceEnabled_ = false;
            asharia::logError("Failed to create editor layout directory: " +
                              directoryError.message());
        }

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
        initInfo.DescriptorPoolSize = kEditorImGuiDescriptorPoolSize;
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

    void ImGuiRuntime::saveLayoutNow() {
        if (contextCreated_ && layoutPersistenceEnabled_ && !layoutIniPathUtf8_.empty()) {
            ImGui::SaveIniSettingsToDisk(layoutIniPathUtf8_.c_str());
        }
    }

    void ImGuiRuntime::shutdown() {
        saveLayoutNow();

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
        layoutPersistenceEnabled_ = false;
    }

    bool ImGuiRuntime::layoutPersistenceEnabled() const {
        return layoutPersistenceEnabled_;
    }

    const std::filesystem::path& ImGuiRuntime::layoutIniPath() const {
        return layoutIniPath_;
    }

} // namespace asharia::editor
