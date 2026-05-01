#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>

#include "vke/core/result.hpp"
#include "vke/rhi_vulkan/vma_fwd.hpp"

namespace vke {

    enum class VulkanBufferMemoryUsage {
        HostUpload,
        DeviceLocal,
    };

    struct VulkanBufferDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        VkDeviceSize size{};
        VkBufferUsageFlags usage{};
        VulkanBufferMemoryUsage memoryUsage{VulkanBufferMemoryUsage::DeviceLocal};
    };

    class VulkanBuffer {
    public:
        VulkanBuffer() = default;
        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;
        VulkanBuffer(VulkanBuffer&& other) noexcept;
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;
        ~VulkanBuffer();

        [[nodiscard]] static Result<VulkanBuffer> create(const VulkanBufferDesc& desc);

        [[nodiscard]] Result<void> upload(std::span<const std::byte> bytes);
        [[nodiscard]] VkBuffer handle() const;
        [[nodiscard]] VkDeviceSize size() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};
        VkBuffer buffer_{VK_NULL_HANDLE};
        VkDeviceSize size_{};
    };

} // namespace vke
