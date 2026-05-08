#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

#include "vke/core/result.hpp"
#include "vke/rhi_vulkan/vma_fwd.hpp"

namespace vke {

    struct VulkanFrameRecordContext;

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
        [[nodiscard]] bool deferDestroy(const VulkanFrameRecordContext& frame);

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
        [[nodiscard]] bool deferDestroy(const VulkanFrameRecordContext& frame);

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkImageView imageView_{VK_NULL_HANDLE};
    };

    struct VulkanTransientImageKey {
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageUsageFlags usage{};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
    };

    struct VulkanTransientImageResource {
        VulkanImage image;
        VulkanImageView imageView;
        VulkanTransientImageKey key;
    };

    struct VulkanTransientImagePoolStats {
        std::uint64_t created{};
        std::uint64_t reused{};
        std::uint64_t released{};
        std::uint64_t retired{};
        std::uint64_t pending{};
        std::uint64_t available{};
    };

    class VulkanTransientImagePool {
    public:
        VulkanTransientImagePool();
        VulkanTransientImagePool(const VulkanTransientImagePool&) = delete;
        VulkanTransientImagePool& operator=(const VulkanTransientImagePool&) = delete;
        VulkanTransientImagePool(VulkanTransientImagePool&& other) noexcept;
        VulkanTransientImagePool& operator=(VulkanTransientImagePool&& other) noexcept;
        ~VulkanTransientImagePool();

        [[nodiscard]] Result<VulkanTransientImageResource> acquire(const VulkanImageDesc& desc);
        [[nodiscard]] Result<void> release(const VulkanFrameRecordContext& frame,
                                           VulkanTransientImageResource& resource);
        [[nodiscard]] VulkanTransientImagePoolStats stats() const;

    private:
        struct State;

        std::shared_ptr<State> state_;
    };

    struct VulkanSamplerDesc {
        VkDevice device{VK_NULL_HANDLE};
        VkFilter minFilter{VK_FILTER_LINEAR};
        VkFilter magFilter{VK_FILTER_LINEAR};
        VkSamplerAddressMode addressModeU{VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
        VkSamplerAddressMode addressModeV{VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
        VkSamplerAddressMode addressModeW{VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    };

    class VulkanSampler {
    public:
        VulkanSampler() = default;
        VulkanSampler(const VulkanSampler&) = delete;
        VulkanSampler& operator=(const VulkanSampler&) = delete;
        VulkanSampler(VulkanSampler&& other) noexcept;
        VulkanSampler& operator=(VulkanSampler&& other) noexcept;
        ~VulkanSampler();

        [[nodiscard]] static Result<VulkanSampler> create(const VulkanSamplerDesc& desc);

        [[nodiscard]] VkSampler handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkSampler sampler_{VK_NULL_HANDLE};
    };

} // namespace vke
