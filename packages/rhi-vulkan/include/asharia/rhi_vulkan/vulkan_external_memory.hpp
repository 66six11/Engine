#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vma_fwd.hpp"

namespace asharia {

    struct VulkanExternalImageDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
    };

    struct VulkanExportedOpaqueWin32Handle {
        void* handle{};
    };

    class VulkanExternalImage {
    public:
        VulkanExternalImage() = default;
        VulkanExternalImage(const VulkanExternalImage&) = delete;
        VulkanExternalImage& operator=(const VulkanExternalImage&) = delete;
        VulkanExternalImage(VulkanExternalImage&& other) noexcept;
        VulkanExternalImage& operator=(VulkanExternalImage&& other) noexcept;
        ~VulkanExternalImage();

        [[nodiscard]] static Result<VulkanExternalImage> create(const VulkanExternalImageDesc& desc);
        [[nodiscard]] Result<VulkanExportedOpaqueWin32Handle> exportOpaqueWin32Handle() const;

        [[nodiscard]] VkImage image() const;
        [[nodiscard]] VkImageView imageView() const;
        [[nodiscard]] VkFormat format() const;
        [[nodiscard]] VkExtent2D extent() const;
        [[nodiscard]] VkImageAspectFlags aspectMask() const;
        [[nodiscard]] std::uint64_t memorySizeBytes() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VmaPool pool_{};
        VmaAllocation allocation_{};
        VkImage image_{VK_NULL_HANDLE};
        VkImageView imageView_{VK_NULL_HANDLE};
        VkFormat format_{VK_FORMAT_UNDEFINED};
        VkExtent2D extent_{};
        VkImageAspectFlags aspectMask_{VK_IMAGE_ASPECT_COLOR_BIT};
        std::uint64_t memorySizeBytes_{};
        std::unique_ptr<VkExportMemoryAllocateInfo> exportMemoryAllocateInfo_;
    };

} // namespace asharia
