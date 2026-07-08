#include "asharia/rhi_vulkan/vulkan_external_memory.hpp"

#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vulkan/vulkan_win32.h>

#include <utility>
#include <vk_mem_alloc.h>

#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia {

    VulkanExternalImage::VulkanExternalImage(VulkanExternalImage&& other) noexcept {
        *this = std::move(other);
    }

    VulkanExternalImage& VulkanExternalImage::operator=(VulkanExternalImage&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        allocator_ = std::exchange(other.allocator_, nullptr);
        pool_ = std::exchange(other.pool_, nullptr);
        allocation_ = std::exchange(other.allocation_, nullptr);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        imageView_ = std::exchange(other.imageView_, VK_NULL_HANDLE);
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        extent_ = std::exchange(other.extent_, VkExtent2D{});
        aspectMask_ = std::exchange(other.aspectMask_, VK_IMAGE_ASPECT_COLOR_BIT);
        memorySizeBytes_ = std::exchange(other.memorySizeBytes_, 0U);
        exportMemoryAllocateInfo_ = std::move(other.exportMemoryAllocateInfo_);
        return *this;
    }

    VulkanExternalImage::~VulkanExternalImage() {
        destroy();
    }

    void VulkanExternalImage::destroy() {
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, imageView_, nullptr);
        }

        if (image_ != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, image_, allocation_);
        }

        if (pool_ != nullptr) {
            vmaDestroyPool(allocator_, pool_);
        }

        device_ = VK_NULL_HANDLE;
        allocator_ = nullptr;
        pool_ = nullptr;
        allocation_ = nullptr;
        image_ = VK_NULL_HANDLE;
        imageView_ = VK_NULL_HANDLE;
        format_ = VK_FORMAT_UNDEFINED;
        extent_ = {};
        aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
        memorySizeBytes_ = 0U;
        exportMemoryAllocateInfo_.reset();
    }

    Result<VulkanExternalImage> VulkanExternalImage::create(const VulkanExternalImageDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.allocator == nullptr ||
            desc.format == VK_FORMAT_UNDEFINED || desc.extent.width == 0 ||
            desc.extent.height == 0 || desc.usage == 0 || desc.aspectMask == 0) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan external image from incomplete inputs")};
        }

        VkExternalMemoryImageCreateInfo externalInfo{};
        externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = &externalInfo;
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
        allocationInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        std::uint32_t memoryTypeIndex = 0;
        VkResult result = vmaFindMemoryTypeIndexForImageInfo(
            desc.allocator, &imageInfo, &allocationInfo, &memoryTypeIndex);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to find memory type for Vulkan external image", result)};
        }

        VmaPoolCreateInfo poolInfo{};
        poolInfo.memoryTypeIndex = memoryTypeIndex;

        VulkanExternalImage image;
        image.device_ = desc.device;
        image.allocator_ = desc.allocator;
        image.format_ = desc.format;
        image.extent_ = desc.extent;
        image.aspectMask_ = desc.aspectMask;
        image.exportMemoryAllocateInfo_ = std::make_unique<VkExportMemoryAllocateInfo>();
        image.exportMemoryAllocateInfo_->sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        image.exportMemoryAllocateInfo_->handleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        poolInfo.pMemoryAllocateNext = image.exportMemoryAllocateInfo_.get();

        result = vmaCreatePool(desc.allocator, &poolInfo, &image.pool_);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to create Vulkan external image memory pool", result)};
        }

        allocationInfo.pool = image.pool_;
        VmaAllocationInfo createdAllocationInfo{};
        result = vmaCreateImage(desc.allocator, &imageInfo, &allocationInfo, &image.image_,
                                &image.allocation_, &createdAllocationInfo);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to create Vulkan external image", result)};
        }
        image.memorySizeBytes_ = static_cast<std::uint64_t>(createdAllocationInfo.size);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image.image_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = image.format_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = image.aspectMask_;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        result = vkCreateImageView(image.device_, &viewInfo, nullptr, &image.imageView_);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to create Vulkan external image view", result)};
        }

        return image;
    }

    Result<VulkanExportedOpaqueWin32Handle> VulkanExternalImage::exportOpaqueWin32Handle() const {
        if (allocator_ == nullptr || allocation_ == nullptr) {
            return std::unexpected{
                vulkanError("Cannot export a Win32 handle from an empty Vulkan external image")};
        }

        HANDLE handle = nullptr;
        const VkResult result = vmaGetMemoryWin32Handle(allocator_, allocation_, nullptr, &handle);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to export Vulkan external image memory handle", result)};
        }

        return VulkanExportedOpaqueWin32Handle{.handle = handle};
    }

    VkImage VulkanExternalImage::image() const {
        return image_;
    }

    VkImageView VulkanExternalImage::imageView() const {
        return imageView_;
    }

    VkFormat VulkanExternalImage::format() const {
        return format_;
    }

    VkExtent2D VulkanExternalImage::extent() const {
        return extent_;
    }

    VkImageAspectFlags VulkanExternalImage::aspectMask() const {
        return aspectMask_;
    }

    std::uint64_t VulkanExternalImage::memorySizeBytes() const {
        return memorySizeBytes_;
    }

} // namespace asharia
