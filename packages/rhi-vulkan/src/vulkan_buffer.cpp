#include "asharia/rhi_vulkan/vulkan_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include <vk_mem_alloc.h>

#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia {
    namespace {

        VmaMemoryUsage vmaMemoryUsage(VulkanBufferMemoryUsage usage) {
            switch (usage) {
            case VulkanBufferMemoryUsage::HostUpload:
            case VulkanBufferMemoryUsage::HostReadback:
                return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            case VulkanBufferMemoryUsage::DeviceLocal:
            default:
                return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            }
        }

        VmaAllocationCreateFlags vmaAllocationFlags(VulkanBufferMemoryUsage usage) {
            switch (usage) {
            case VulkanBufferMemoryUsage::HostUpload:
                return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;
            case VulkanBufferMemoryUsage::HostReadback:
                return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;
            case VulkanBufferMemoryUsage::DeviceLocal:
            default:
                return {};
            }
        }

    } // namespace

    VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept {
        *this = std::move(other);
    }

    VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        allocator_ = std::exchange(other.allocator_, nullptr);
        allocation_ = std::exchange(other.allocation_, nullptr);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        size_ = std::exchange(other.size_, VkDeviceSize{});
        stats_ = std::exchange(other.stats_, VulkanBufferStats{});
        return *this;
    }

    VulkanBuffer::~VulkanBuffer() {
        destroy();
    }

    void VulkanBuffer::destroy() {
        if (buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, buffer_, allocation_);
        }

        device_ = VK_NULL_HANDLE;
        allocator_ = nullptr;
        allocation_ = nullptr;
        buffer_ = VK_NULL_HANDLE;
        size_ = {};
        stats_ = {};
    }

    Result<VulkanBuffer> VulkanBuffer::create(const VulkanBufferDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.allocator == nullptr || desc.size == 0 ||
            desc.usage == 0) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan buffer from incomplete inputs")};
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = desc.size;
        bufferInfo.usage = desc.usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = vmaMemoryUsage(desc.memoryUsage);
        allocationInfo.flags = vmaAllocationFlags(desc.memoryUsage);

        VulkanBuffer buffer;
        buffer.device_ = desc.device;
        buffer.allocator_ = desc.allocator;
        buffer.size_ = desc.size;
        buffer.stats_.created = 1;
        buffer.stats_.allocatedBytes = desc.size;
        switch (desc.memoryUsage) {
        case VulkanBufferMemoryUsage::HostUpload:
            buffer.stats_.hostUploadCreated = 1;
            break;
        case VulkanBufferMemoryUsage::HostReadback:
            buffer.stats_.hostReadbackCreated = 1;
            break;
        case VulkanBufferMemoryUsage::DeviceLocal:
        default:
            buffer.stats_.deviceLocalCreated = 1;
            break;
        }

        const VkResult result = vmaCreateBuffer(desc.allocator, &bufferInfo, &allocationInfo,
                                                &buffer.buffer_, &buffer.allocation_, nullptr);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan buffer", result)};
        }

        return buffer;
    }

    Result<void> VulkanBuffer::upload(std::span<const std::byte> bytes) {
        if (buffer_ == VK_NULL_HANDLE || allocation_ == nullptr || allocator_ == nullptr) {
            return std::unexpected{vulkanError("Cannot upload to an uninitialized Vulkan buffer")};
        }
        if (bytes.size_bytes() > size_) {
            return std::unexpected{vulkanError("Cannot upload more data than the Vulkan buffer holds")};
        }

        void* mapped = nullptr;
        const VkResult mapResult = vmaMapMemory(allocator_, allocation_, &mapped);
        if (mapResult != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to map Vulkan buffer memory", mapResult)};
        }

        std::memcpy(mapped, bytes.data(), bytes.size_bytes());
        const VkResult flushResult = vmaFlushAllocation(allocator_, allocation_, 0,
                                                        bytes.size_bytes());
        vmaUnmapMemory(allocator_, allocation_);
        if (flushResult != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to flush Vulkan buffer memory", flushResult)};
        }

        ++stats_.uploadCalls;
        stats_.uploadedBytes += bytes.size_bytes();
        return {};
    }

    Result<void> VulkanBuffer::read(std::span<std::byte> bytes) {
        if (buffer_ == VK_NULL_HANDLE || allocation_ == nullptr || allocator_ == nullptr) {
            return std::unexpected{vulkanError("Cannot read from an uninitialized Vulkan buffer")};
        }
        if (bytes.size_bytes() > size_) {
            return std::unexpected{
                vulkanError("Cannot read more data than the Vulkan buffer holds")};
        }

        const auto byteCount = static_cast<VkDeviceSize>(bytes.size_bytes());
        const VkResult invalidateResult = vmaInvalidateAllocation(allocator_, allocation_, 0,
                                                                  byteCount);
        if (invalidateResult != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to invalidate Vulkan buffer memory", invalidateResult)};
        }

        void* mapped = nullptr;
        const VkResult mapResult = vmaMapMemory(allocator_, allocation_, &mapped);
        if (mapResult != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to map Vulkan buffer memory", mapResult)};
        }

        std::memcpy(bytes.data(), mapped, bytes.size_bytes());
        vmaUnmapMemory(allocator_, allocation_);
        return {};
    }

    VkBuffer VulkanBuffer::handle() const {
        return buffer_;
    }

    VkDeviceSize VulkanBuffer::size() const {
        return size_;
    }

    VulkanBufferStats VulkanBuffer::stats() const {
        return stats_;
    }

} // namespace asharia
