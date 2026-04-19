#pragma once

#include "vke/core/result.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;

namespace vke {

struct VulkanContextDesc {
    std::string applicationName{"VkEngine"};
    std::span<const std::string> requiredInstanceExtensions{};
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
    [[nodiscard]] VkPhysicalDevice physicalDevice() const;
    [[nodiscard]] VkDevice device() const;
    [[nodiscard]] VkQueue graphicsQueue() const;
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const;
    [[nodiscard]] VmaAllocator allocator() const;
    [[nodiscard]] const VulkanDeviceInfo& deviceInfo() const;

private:
    void destroy();

    VkInstance instance_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    std::uint32_t graphicsQueueFamily_{0};
    VmaAllocator allocator_{nullptr};
    VulkanDeviceInfo deviceInfo_{};
};

[[nodiscard]] std::string vkResultName(VkResult result);
[[nodiscard]] std::string vulkanVersionString(std::uint32_t version);

} // namespace vke
