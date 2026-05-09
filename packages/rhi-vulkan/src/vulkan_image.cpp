#include "vke/rhi_vulkan/vulkan_image.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include <vk_mem_alloc.h>

#include "vke/rhi_vulkan/vulkan_error.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"

namespace vke {
    namespace {

        [[nodiscard]] bool sameTransientImageKey(const VulkanTransientImageKey& left,
                                                 const VulkanTransientImageKey& right) {
            return left.format == right.format && left.extent.width == right.extent.width &&
                   left.extent.height == right.extent.height && left.usage == right.usage &&
                   left.aspectMask == right.aspectMask;
        }

        [[nodiscard]] VulkanTransientImageKey transientImageKey(const VulkanImageDesc& desc) {
            return VulkanTransientImageKey{
                .format = desc.format,
                .extent = desc.extent,
                .usage = desc.usage,
                .aspectMask = desc.aspectMask,
            };
        }

        [[nodiscard]] bool hasTransientResource(const VulkanTransientImageResource& resource) {
            return resource.image.handle() != VK_NULL_HANDLE ||
                   resource.imageView.handle() != VK_NULL_HANDLE;
        }

    } // namespace

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

    bool VulkanImage::deferDestroy(const VulkanFrameRecordContext& frame) {
        if (image_ == VK_NULL_HANDLE) {
            return true;
        }

        const VmaAllocator allocator = allocator_;
        const VmaAllocation allocation = allocation_;
        const VkImage image = image_;
        if (!frame.deferDeletion([allocator, allocation, image]() {
                vmaDestroyImage(allocator, image, allocation);
            })) {
            return false;
        }

        device_ = VK_NULL_HANDLE;
        allocator_ = nullptr;
        allocation_ = nullptr;
        image_ = VK_NULL_HANDLE;
        format_ = VK_FORMAT_UNDEFINED;
        extent_ = {};
        aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
        return true;
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
        VkImageView* const imageViewOut = &imageView.imageView_;
        const VkResult result = vkCreateImageView(desc.device, &createInfo, nullptr, imageViewOut);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan image view", result)};
        }

        return imageView;
    }

    VkImageView VulkanImageView::handle() const {
        return imageView_;
    }

    bool VulkanImageView::deferDestroy(const VulkanFrameRecordContext& frame) {
        if (imageView_ == VK_NULL_HANDLE) {
            return true;
        }

        const VkDevice device = device_;
        const VkImageView imageView = imageView_;
        if (!frame.deferDeletion(
                [device, imageView]() { vkDestroyImageView(device, imageView, nullptr); })) {
            return false;
        }

        device_ = VK_NULL_HANDLE;
        imageView_ = VK_NULL_HANDLE;
        return true;
    }

    VulkanRenderTarget::VulkanRenderTarget(VulkanRenderTarget&& other) noexcept {
        *this = std::move(other);
    }

    VulkanRenderTarget& VulkanRenderTarget::operator=(VulkanRenderTarget&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        imageView_ = std::move(other.imageView_);
        image_ = std::move(other.image_);
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        extent_ = std::exchange(other.extent_, VkExtent2D{});
        usage_ = std::exchange(other.usage_, VkImageUsageFlags{});
        aspectMask_ = std::exchange(other.aspectMask_, VK_IMAGE_ASPECT_COLOR_BIT);
        stats_ = std::exchange(other.stats_, {});
        return *this;
    }

    bool VulkanRenderTarget::matches(const VulkanRenderTargetDesc& desc) const {
        return image_.handle() != VK_NULL_HANDLE && imageView_.handle() != VK_NULL_HANDLE &&
               format_ == desc.format && extent_.width == desc.extent.width &&
               extent_.height == desc.extent.height && usage_ == desc.usage &&
               aspectMask_ == desc.aspectMask;
    }

    bool VulkanRenderTarget::hasResource() const {
        return image_.handle() != VK_NULL_HANDLE || imageView_.handle() != VK_NULL_HANDLE;
    }

    Result<void> VulkanRenderTarget::ensure(const VulkanFrameRecordContext& frame,
                                            const VulkanRenderTargetDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.allocator == nullptr ||
            desc.format == VK_FORMAT_UNDEFINED || desc.extent.width == 0 ||
            desc.extent.height == 0 || desc.usage == 0 || desc.aspectMask == 0) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan render target from incomplete inputs")};
        }

        if (matches(desc)) {
            ++stats_.reused;
            return {};
        }

        auto image = VulkanImage::create(VulkanImageDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .format = desc.format,
            .extent = desc.extent,
            .usage = desc.usage,
            .aspectMask = desc.aspectMask,
        });
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }

        auto imageView = VulkanImageView::create(VulkanImageViewDesc{
            .device = desc.device,
            .image = image->handle(),
            .format = image->format(),
            .aspectMask = image->aspectMask(),
        });
        if (!imageView) {
            return std::unexpected{std::move(imageView.error())};
        }

        if (hasResource()) {
            if (frame.frameLoop == nullptr) {
                return std::unexpected{
                    vulkanError("Cannot replace a Vulkan render target without deferred deletion")};
            }
            if (!imageView_.deferDestroy(frame)) {
                return std::unexpected{
                    vulkanError("Failed to defer destruction of Vulkan render target image view")};
            }
            if (!image_.deferDestroy(frame)) {
                return std::unexpected{
                    vulkanError("Failed to defer destruction of Vulkan render target image")};
            }
            ++stats_.deferredForDeletion;
        }

        image_ = std::move(*image);
        imageView_ = std::move(*imageView);
        format_ = desc.format;
        extent_ = desc.extent;
        usage_ = desc.usage;
        aspectMask_ = desc.aspectMask;
        ++stats_.created;
        return {};
    }

    bool VulkanRenderTarget::deferDestroy(const VulkanFrameRecordContext& frame) {
        if (!hasResource()) {
            return true;
        }
        if (!imageView_.deferDestroy(frame)) {
            return false;
        }
        if (!image_.deferDestroy(frame)) {
            return false;
        }

        format_ = VK_FORMAT_UNDEFINED;
        extent_ = {};
        usage_ = {};
        aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
        ++stats_.deferredForDeletion;
        return true;
    }

    VulkanSampledTextureView VulkanRenderTarget::sampledTextureView() const {
        return VulkanSampledTextureView{
            .image = image_.handle(),
            .imageView = imageView_.handle(),
            .format = format_,
            .extent = extent_,
            .aspectMask = aspectMask_,
            .sampledLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    }

    VulkanRenderTargetStats VulkanRenderTarget::stats() const {
        return stats_;
    }

    struct VulkanTransientImagePool::State {
        std::vector<VulkanTransientImageResource> available;
        VulkanTransientImagePoolStats stats;
    };

    VulkanTransientImagePool::VulkanTransientImagePool() : state_{std::make_shared<State>()} {}

    VulkanTransientImagePool::VulkanTransientImagePool(VulkanTransientImagePool&& other) noexcept =
        default;

    VulkanTransientImagePool&
    VulkanTransientImagePool::operator=(VulkanTransientImagePool&& other) noexcept = default;

    VulkanTransientImagePool::~VulkanTransientImagePool() = default;

    Result<VulkanTransientImageResource>
    VulkanTransientImagePool::acquire(const VulkanImageDesc& desc) {
        if (state_ == nullptr) {
            return std::unexpected{
                vulkanError("Cannot acquire a transient Vulkan image from a moved pool")};
        }

        const VulkanTransientImageKey key = transientImageKey(desc);
        auto resource = std::ranges::find_if(state_->available,
                                             [&key](const VulkanTransientImageResource& candidate) {
                                                 return sameTransientImageKey(candidate.key, key);
                                             });
        if (resource != state_->available.end()) {
            VulkanTransientImageResource reused = std::move(*resource);
            state_->available.erase(resource);
            state_->stats.available = static_cast<std::uint64_t>(state_->available.size());
            ++state_->stats.reused;
            return reused;
        }

        auto image = VulkanImage::create(desc);
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }

        auto imageView = VulkanImageView::create(VulkanImageViewDesc{
            .device = desc.device,
            .image = image->handle(),
            .format = image->format(),
            .aspectMask = image->aspectMask(),
        });
        if (!imageView) {
            return std::unexpected{std::move(imageView.error())};
        }

        ++state_->stats.created;
        return VulkanTransientImageResource{
            .image = std::move(*image),
            .imageView = std::move(*imageView),
            .key = key,
        };
    }

    Result<void> VulkanTransientImagePool::release(const VulkanFrameRecordContext& frame,
                                                   VulkanTransientImageResource& resource) {
        if (state_ == nullptr) {
            return std::unexpected{
                vulkanError("Cannot release a transient Vulkan image to a moved pool")};
        }
        if (!hasTransientResource(resource)) {
            return {};
        }

        auto state = state_;
        auto retiredResource = std::make_shared<VulkanTransientImageResource>(std::move(resource));
        if (!frame.deferDeletion([state, retiredResource]() mutable {
                state->available.push_back(std::move(*retiredResource));
                ++state->stats.retired;
                --state->stats.pending;
                state->stats.available = static_cast<std::uint64_t>(state->available.size());
            })) {
            resource = std::move(*retiredResource);
            return std::unexpected{
                vulkanError("Failed to enqueue transient Vulkan image pool release")};
        }

        ++state_->stats.released;
        ++state_->stats.pending;
        return {};
    }

    VulkanTransientImagePoolStats VulkanTransientImagePool::stats() const {
        if (state_ == nullptr) {
            return {};
        }

        VulkanTransientImagePoolStats stats = state_->stats;
        stats.available = static_cast<std::uint64_t>(state_->available.size());
        return stats;
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
        VkSampler* const samplerOut = &sampler.sampler_;
        const VkResult result = vkCreateSampler(desc.device, &createInfo, nullptr, samplerOut);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan sampler", result)};
        }

        return sampler;
    }

    VkSampler VulkanSampler::handle() const {
        return sampler_;
    }

} // namespace vke
