#include "imgui_runtime.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

#include "editor_ui.hpp"

namespace {

    void checkImguiVkResult(VkResult result) {
        if (result != VK_SUCCESS) {
            asharia::logError("Dear ImGui Vulkan backend error: " + asharia::vkResultName(result));
        }
    }

    [[nodiscard]] std::string environmentValue(std::string_view name) {
#if defined(_WIN32)
        const std::string nameText{name};
        char* value = nullptr;
        std::size_t valueSize = 0;
        if (_dupenv_s(&value, &valueSize, nameText.c_str()) != 0 || value == nullptr) {
            return {};
        }
        const std::unique_ptr<char, decltype(&std::free)> ownedValue{value, &std::free};
        return std::string{ownedValue.get()};
#else
        const std::string nameText{name};
        if (const char* value = std::getenv(nameText.c_str());
            value != nullptr && value[0] != '\0') {
            return value;
        }
        return {};
#endif
    }

    [[nodiscard]] std::string localAppDataPath() {
        return environmentValue("LOCALAPPDATA");
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

    [[nodiscard]] std::filesystem::path windowsFontsPath() {
#if defined(_WIN32)
        const std::string windowsDirectory = environmentValue("WINDIR");
        if (!windowsDirectory.empty()) {
            return std::filesystem::path{windowsDirectory} / "Fonts";
        }
        return std::filesystem::path{"C:\\Windows\\Fonts"};
#else
        return {};
#endif
    }

    [[nodiscard]] std::vector<std::filesystem::path>
    cjkFontCandidates(const asharia::editor::ImGuiRuntimeDesc& desc, bool& explicitPath) {
        explicitPath = false;
        if (!desc.cjkFontPath.empty()) {
            explicitPath = true;
            return {desc.cjkFontPath};
        }

        const std::string environmentPath = environmentValue("ASHARIA_EDITOR_CJK_FONT");
        if (!environmentPath.empty()) {
            explicitPath = true;
            return {std::filesystem::path{environmentPath}};
        }

#if defined(_WIN32)
        const std::filesystem::path fontsPath = windowsFontsPath();
        return {
            fontsPath / "NotoSansSC-VF.ttf", fontsPath / "msyh.ttc", fontsPath / "simhei.ttf",
            fontsPath / "simsun.ttc",        fontsPath / "Deng.ttf",
        };
#else
        return {
            std::filesystem::path{"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"},
            std::filesystem::path{"/usr/share/fonts/opentype/noto/NotoSansSC-Regular.otf"},
            std::filesystem::path{"/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"},
        };
#endif
    }

    [[nodiscard]] std::optional<std::filesystem::path>
    chooseCjkFontPath(const asharia::editor::ImGuiRuntimeDesc& desc, bool& explicitPath) {
        for (const std::filesystem::path& candidate : cjkFontCandidates(desc, explicitPath)) {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool readBinaryFile(const std::filesystem::path& path, std::vector<char>& bytes) {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file) {
            return false;
        }

        const std::streamoff fileSize = file.tellg();
        if (fileSize <= 0 || fileSize > std::numeric_limits<int>::max()) {
            return false;
        }

        bytes.assign(static_cast<std::size_t>(fileSize), '\0');
        file.seekg(0, std::ios::beg);
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return file.good();
    }

    void configureEditorFonts(const asharia::editor::ImGuiRuntimeDesc& desc,
                              asharia::editor::ImGuiRuntimeFontStatus& status,
                              std::vector<char>& fontData) {
        status = asharia::editor::ImGuiRuntimeFontStatus{};
        fontData.clear();
        if (!desc.enableCjkGlyphs) {
            return;
        }

        status.cjkRequested = true;
        bool explicitPath = false;
        const std::optional<std::filesystem::path> fontPath = chooseCjkFontPath(desc, explicitPath);
        status.cjkFontPathExplicit = explicitPath;
        if (!fontPath) {
            asharia::logWarning(
                "Editor CJK font was requested, but no usable font file was found.");
            return;
        }

        status.cjkCandidateFound = true;
        status.cjkFontPath = *fontPath;
        if (!readBinaryFile(*fontPath, fontData)) {
            asharia::logError("Failed to read editor CJK font: " + pathToUtf8String(*fontPath));
            return;
        }

        ImGuiIO& imguiIo = ImGui::GetIO();
        ImFontConfig fontConfig{};
        fontConfig.FontDataOwnedByAtlas = false;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 1;
        const float fontSize = desc.fontPixelSize > 0.0F ? desc.fontPixelSize : 16.0F;
        const ImWchar* glyphRanges = imguiIo.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        ImFont* font = imguiIo.Fonts->AddFontFromMemoryTTF(
            fontData.data(), static_cast<int>(fontData.size()), fontSize, &fontConfig, glyphRanges);
        if (font == nullptr) {
            fontData.clear();
            asharia::logError("Failed to register editor CJK font: " + pathToUtf8String(*fontPath));
            return;
        }

        imguiIo.FontDefault = font;
        status.cjkLoaded = true;
        asharia::logInfo("Loaded editor CJK font: " + pathToUtf8String(*fontPath));
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
        configureEditorFonts(desc, fontStatus_, cjkFontData_);

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

        cjkFontData_.clear();
        queue_ = VK_NULL_HANDLE;
        layoutPersistenceEnabled_ = false;
    }

    bool ImGuiRuntime::layoutPersistenceEnabled() const {
        return layoutPersistenceEnabled_;
    }

    const std::filesystem::path& ImGuiRuntime::layoutIniPath() const {
        return layoutIniPath_;
    }

    const ImGuiRuntimeFontStatus& ImGuiRuntime::fontStatus() const {
        return fontStatus_;
    }

} // namespace asharia::editor
