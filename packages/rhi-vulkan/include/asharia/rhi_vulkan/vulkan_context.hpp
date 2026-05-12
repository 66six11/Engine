#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vma_fwd.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia {

    using VulkanSurfaceFactory = std::function<Result<VkSurfaceKHR>(VkInstance)>;

    enum class VulkanDebugLabelMode {
        Disabled,
        Optional,
        Required,
    };

    struct VulkanContextDesc {
        std::string applicationName{"Asharia Engine"};
        std::span<const std::string> requiredInstanceExtensions{};
        VulkanSurfaceFactory createSurface{};
        bool enableValidation{true};
        VulkanDebugLabelMode debugLabels{VulkanDebugLabelMode::Optional};
        bool requireVulkan14{true};
    };

    struct VulkanDeviceInfo {
        std::string name;
        std::uint32_t vendorId{};
        std::uint32_t deviceId{};
        std::uint32_t apiVersion{};
        std::uint32_t graphicsQueueFamily{};
        bool graphicsQueueSupportsCompute{};
        std::uint32_t graphicsQueueTimestampValidBits{};
        float timestampPeriodNanoseconds{};
    };

    struct VulkanDebugLabelFunctions {
        PFN_vkCmdBeginDebugUtilsLabelEXT beginCommandLabel{};
        PFN_vkCmdEndDebugUtilsLabelEXT endCommandLabel{};
        PFN_vkSetDebugUtilsObjectNameEXT setObjectName{};
    };

    class VulkanContext {
    public:
        VulkanContext() = default;
        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;
        VulkanContext(VulkanContext&& other) noexcept;
        VulkanContext& operator=(VulkanContext&& other) noexcept;
        ~VulkanContext();

        [[nodiscard]] static Result<VulkanContext> create(const VulkanContextDesc& desc);

        [[nodiscard]] VkInstance instance() const;
        [[nodiscard]] std::uint32_t instanceApiVersion() const;
        [[nodiscard]] VkSurfaceKHR surface() const;
        [[nodiscard]] VkPhysicalDevice physicalDevice() const;
        [[nodiscard]] VkDevice device() const;
        [[nodiscard]] VkQueue graphicsQueue() const;
        [[nodiscard]] std::uint32_t graphicsQueueFamily() const;
        [[nodiscard]] VmaAllocator allocator() const;
        [[nodiscard]] const VulkanDeviceInfo& deviceInfo() const;
        [[nodiscard]] VulkanDebugLabelFunctions debugLabelFunctions() const;

    private:
        void destroy();

        VkInstance instance_{VK_NULL_HANDLE};
        std::uint32_t instanceApiVersion_{VK_API_VERSION_1_3};
        VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
        VkSurfaceKHR surface_{VK_NULL_HANDLE};
        VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
        VkDevice device_{VK_NULL_HANDLE};
        VkQueue graphicsQueue_{VK_NULL_HANDLE};
        std::uint32_t graphicsQueueFamily_{0};
        VmaAllocator allocator_{nullptr};
        VulkanDeviceInfo deviceInfo_{};
        VulkanDebugLabelFunctions debugLabelFunctions_{};
    };

} // namespace asharia
