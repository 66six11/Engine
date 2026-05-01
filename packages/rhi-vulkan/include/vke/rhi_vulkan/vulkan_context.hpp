#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vke/core/result.hpp"
#include "vke/rhi_vulkan/vma_fwd.hpp"
#include "vke/rhi_vulkan/vulkan_error.hpp"

namespace vke {

    using VulkanSurfaceFactory = std::function<Result<VkSurfaceKHR>(VkInstance)>;

    struct VulkanContextDesc {
        std::string applicationName{"VkEngine"};
        std::span<const std::string> requiredInstanceExtensions{};
        VulkanSurfaceFactory createSurface{};
        bool enableValidation{true};
        bool requireVulkan14{true};
    };

    struct VulkanDeviceInfo {
        std::string name;
        std::uint32_t vendorId{};
        std::uint32_t deviceId{};
        std::uint32_t apiVersion{};
        std::uint32_t graphicsQueueFamily{};
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
    };

} // namespace vke
