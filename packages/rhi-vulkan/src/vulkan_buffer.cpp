#include "vke/rhi_vulkan/vulkan_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include <vk_mem_alloc.h>

#include "vke/core/error.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"

namespace vke {
    namespace {

        Error vkError(std::string message, VkResult result = VK_ERROR_UNKNOWN) {
            if (result != VK_SUCCESS) {
                message += ": ";
                message += vkResultName(result);
            }

            return Error{ErrorDomain::Vulkan, static_cast<int>(result), std::move(message)};
        }

        VmaMemoryUsage vmaMemoryUsage(VulkanBufferMemoryUsage usage) {
            switch (usage) {
            case VulkanBufferMemoryUsage::HostUpload:
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
    }

    Result<VulkanBuffer> VulkanBuffer::create(const VulkanBufferDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.allocator == nullptr || desc.size == 0 ||
            desc.usage == 0) {
            return std::unexpected{
                vkError("Cannot create a Vulkan buffer from incomplete inputs")};
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

        const VkResult result = vmaCreateBuffer(desc.allocator, &bufferInfo, &allocationInfo,
                                                &buffer.buffer_, &buffer.allocation_, nullptr);
        if (result != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to create Vulkan buffer", result)};
        }

        return buffer;
    }

    Result<void> VulkanBuffer::upload(std::span<const std::byte> bytes) {
        if (buffer_ == VK_NULL_HANDLE || allocation_ == nullptr || allocator_ == nullptr) {
            return std::unexpected{vkError("Cannot upload to an uninitialized Vulkan buffer")};
        }
        if (bytes.size_bytes() > size_) {
            return std::unexpected{vkError("Cannot upload more data than the Vulkan buffer holds")};
        }

        void* mapped = nullptr;
        const VkResult mapResult = vmaMapMemory(allocator_, allocation_, &mapped);
        if (mapResult != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to map Vulkan buffer memory", mapResult)};
        }

        std::memcpy(mapped, bytes.data(), bytes.size_bytes());
        const VkResult flushResult = vmaFlushAllocation(allocator_, allocation_, 0,
                                                        bytes.size_bytes());
        vmaUnmapMemory(allocator_, allocation_);
        if (flushResult != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to flush Vulkan buffer memory", flushResult)};
        }

        return {};
    }

    VkBuffer VulkanBuffer::handle() const {
        return buffer_;
    }

    VkDeviceSize VulkanBuffer::size() const {
        return size_;
    }

} // namespace vke
