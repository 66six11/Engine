#pragma once

#include <vulkan/vulkan.h>

#include "vke/core/result.hpp"
#include "vke/rhi_vulkan/vma_fwd.hpp"

namespace vke {

    struct VulkanImageDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
    };

    class VulkanImage {
    public:
        VulkanImage() = default;
        VulkanImage(const VulkanImage&) = delete;
        VulkanImage& operator=(const VulkanImage&) = delete;
        VulkanImage(VulkanImage&& other) noexcept;
        VulkanImage& operator=(VulkanImage&& other) noexcept;
        ~VulkanImage();

        [[nodiscard]] static Result<VulkanImage> create(const VulkanImageDesc& desc);

        [[nodiscard]] VkImage handle() const;
        [[nodiscard]] VkFormat format() const;
        [[nodiscard]] VkExtent2D extent() const;
        [[nodiscard]] VkImageAspectFlags aspectMask() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VmaAllocation allocation_{};
        VkImage image_{VK_NULL_HANDLE};
        VkFormat format_{VK_FORMAT_UNDEFINED};
        VkExtent2D extent_{};
        VkImageAspectFlags aspectMask_{VK_IMAGE_ASPECT_COLOR_BIT};
    };

    struct VulkanImageViewDesc {
        VkDevice device{VK_NULL_HANDLE};
        VkImage image{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
    };

    class VulkanImageView {
    public:
        VulkanImageView() = default;
        VulkanImageView(const VulkanImageView&) = delete;
        VulkanImageView& operator=(const VulkanImageView&) = delete;
        VulkanImageView(VulkanImageView&& other) noexcept;
        VulkanImageView& operator=(VulkanImageView&& other) noexcept;
        ~VulkanImageView();

        [[nodiscard]] static Result<VulkanImageView> create(const VulkanImageViewDesc& desc);

        [[nodiscard]] VkImageView handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkImageView imageView_{VK_NULL_HANDLE};
    };

} // namespace vke
