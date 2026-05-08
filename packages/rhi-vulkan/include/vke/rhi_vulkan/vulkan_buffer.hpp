#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
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

    struct VulkanBufferStats {
        std::uint64_t created{};
        std::uint64_t hostUploadCreated{};
        std::uint64_t deviceLocalCreated{};
        std::uint64_t allocatedBytes{};
        std::uint64_t uploadCalls{};
        std::uint64_t uploadedBytes{};
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
        [[nodiscard]] VulkanBufferStats stats() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};
        VkBuffer buffer_{VK_NULL_HANDLE};
        VkDeviceSize size_{};
        VulkanBufferStats stats_;
    };

} // namespace vke
