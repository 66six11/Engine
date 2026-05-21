#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"

struct GLFWwindow;

namespace asharia::editor {

    inline constexpr std::uint32_t kEditorImGuiDescriptorPoolSize = 128;

    struct ImGuiRuntimeDesc {
        std::filesystem::path layoutIniPath;
    };

    class ImGuiRuntime {
    public:
        ImGuiRuntime() = default;
        ImGuiRuntime(const ImGuiRuntime&) = delete;
        ImGuiRuntime& operator=(const ImGuiRuntime&) = delete;
        ImGuiRuntime(ImGuiRuntime&&) = delete;
        ImGuiRuntime& operator=(ImGuiRuntime&&) = delete;

        ~ImGuiRuntime();

        [[nodiscard]] asharia::VoidResult create(GLFWwindow* window,
                                                 const asharia::VulkanContext& context,
                                                 const asharia::VulkanFrameLoop& frameLoop,
                                                 const ImGuiRuntimeDesc& desc = {});
        void saveLayoutNow();
        void shutdown();
        [[nodiscard]] bool layoutPersistenceEnabled() const;
        [[nodiscard]] const std::filesystem::path& layoutIniPath() const;

    private:
        std::filesystem::path layoutIniPath_;
        std::string layoutIniPathUtf8_;
        VkQueue queue_{VK_NULL_HANDLE};
        bool contextCreated_{false};
        bool glfwInitialized_{false};
        bool vulkanInitialized_{false};
        bool layoutPersistenceEnabled_{false};
    };

} // namespace asharia::editor
