#include "vke/rhi_vulkan/vulkan_image.hpp"

#include <utility>

#include <vk_mem_alloc.h>

#include "vke/rhi_vulkan/vulkan_error.hpp"

namespace vke {

    VulkanImage::VulkanImage(VulkanImage&& other) noexcept {
        *this = std::move(other);
    }

    VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        allocator_ = std::exchange(other.allocator_, nullptr);
        allocation_ = std::exchange(other.allocation_, nullptr);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        extent_ = std::exchange(other.extent_, VkExtent2D{});
        aspectMask_ = std::exchange(other.aspectMask_, VK_IMAGE_ASPECT_COLOR_BIT);
        return *this;
    }

    VulkanImage::~VulkanImage() {
        destroy();
    }

    void VulkanImage::destroy() {
        if (image_ != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, image_, allocation_);
        }

        device_ = VK_NULL_HANDLE;
        allocator_ = nullptr;
        allocation_ = nullptr;
        image_ = VK_NULL_HANDLE;
        format_ = VK_FORMAT_UNDEFINED;
        extent_ = {};
        aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    Result<VulkanImage> VulkanImage::create(const VulkanImageDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.allocator == nullptr ||
            desc.format == VK_FORMAT_UNDEFINED || desc.extent.width == 0 ||
            desc.extent.height == 0 || desc.usage == 0 || desc.aspectMask == 0) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan image from incomplete inputs")};
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = desc.format;
        imageInfo.extent = VkExtent3D{
            .width = desc.extent.width,
            .height = desc.extent.height,
            .depth = 1,
        };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = desc.usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VulkanImage image;
        image.device_ = desc.device;
        image.allocator_ = desc.allocator;
        image.format_ = desc.format;
        image.extent_ = desc.extent;
        image.aspectMask_ = desc.aspectMask;

        const VkResult result = vmaCreateImage(desc.allocator, &imageInfo, &allocationInfo,
                                               &image.image_, &image.allocation_, nullptr);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan image", result)};
        }

        return image;
    }

    VkImage VulkanImage::handle() const {
        return image_;
    }

    VkFormat VulkanImage::format() const {
        return format_;
    }

    VkExtent2D VulkanImage::extent() const {
        return extent_;
    }

    VkImageAspectFlags VulkanImage::aspectMask() const {
        return aspectMask_;
    }

    VulkanImageView::VulkanImageView(VulkanImageView&& other) noexcept {
        *this = std::move(other);
    }

    VulkanImageView& VulkanImageView::operator=(VulkanImageView&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        imageView_ = std::exchange(other.imageView_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanImageView::~VulkanImageView() {
        destroy();
    }

    void VulkanImageView::destroy() {
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, imageView_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        imageView_ = VK_NULL_HANDLE;
    }

    Result<VulkanImageView> VulkanImageView::create(const VulkanImageViewDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.image == VK_NULL_HANDLE ||
            desc.format == VK_FORMAT_UNDEFINED || desc.aspectMask == 0) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan image view from incomplete inputs")};
        }

        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = desc.image;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = desc.format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = desc.aspectMask;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VulkanImageView imageView;
        imageView.device_ = desc.device;
        const VkResult result = vkCreateImageView(desc.device, &createInfo, nullptr,
                                                  &imageView.imageView_);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan image view", result)};
        }

        return imageView;
    }

    VkImageView VulkanImageView::handle() const {
        return imageView_;
    }

    VulkanSampler::VulkanSampler(VulkanSampler&& other) noexcept {
        *this = std::move(other);
    }

    VulkanSampler& VulkanSampler::operator=(VulkanSampler&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        sampler_ = std::exchange(other.sampler_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanSampler::~VulkanSampler() {
        destroy();
    }

    void VulkanSampler::destroy() {
        if (sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        sampler_ = VK_NULL_HANDLE;
    }

    Result<VulkanSampler> VulkanSampler::create(const VulkanSamplerDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{vulkanError("Cannot create a Vulkan sampler without a device")};
        }

        VkSamplerCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        createInfo.magFilter = desc.magFilter;
        createInfo.minFilter = desc.minFilter;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        createInfo.addressModeU = desc.addressModeU;
        createInfo.addressModeV = desc.addressModeV;
        createInfo.addressModeW = desc.addressModeW;
        createInfo.mipLodBias = 0.0F;
        createInfo.anisotropyEnable = VK_FALSE;
        createInfo.maxAnisotropy = 1.0F;
        createInfo.compareEnable = VK_FALSE;
        createInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        createInfo.minLod = 0.0F;
        createInfo.maxLod = 0.0F;
        createInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        createInfo.unnormalizedCoordinates = VK_FALSE;

        VulkanSampler sampler;
        sampler.device_ = desc.device;
        const VkResult result = vkCreateSampler(desc.device, &createInfo, nullptr,
                                                &sampler.sampler_);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan sampler", result)};
        }

        return sampler;
    }

    VkSampler VulkanSampler::handle() const {
        return sampler_;
    }

} // namespace vke
