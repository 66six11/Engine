#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "vke/rhi_vulkan/vulkan_error.hpp"

namespace vke {
    namespace {

        Result<VkSurfaceCapabilitiesKHR> querySurfaceCapabilities(VkPhysicalDevice physicalDevice,
                                                                  VkSurfaceKHR surface) {
            VkSurfaceCapabilitiesKHR capabilities{};
            const VkResult result =
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to query Vulkan surface capabilities", result)};
            }

            return capabilities;
        }

        Result<std::vector<VkSurfaceFormatKHR>> querySurfaceFormats(VkPhysicalDevice physicalDevice,
                                                                    VkSurfaceKHR surface) {
            std::uint32_t count = 0;
            VkResult result =
                vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to query Vulkan surface formats", result)};
            }

            std::vector<VkSurfaceFormatKHR> formats(count);
            if (count > 0) {
                result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count,
                                                              formats.data());
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vulkanError("Failed to query Vulkan surface formats", result)};
                }
            }

            return formats;
        }

        Result<std::vector<VkPresentModeKHR>> queryPresentModes(VkPhysicalDevice physicalDevice,
                                                                VkSurfaceKHR surface) {
            std::uint32_t count = 0;
            VkResult result =
                vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to query Vulkan surface present modes", result)};
            }

            std::vector<VkPresentModeKHR> presentModes(count);
            if (count > 0) {
                result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count,
                                                                   presentModes.data());
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vulkanError("Failed to query Vulkan surface present modes", result)};
                }
            }

            return presentModes;
        }

        VkSurfaceFormatKHR chooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats) {
            const auto preferred =
                std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& item) {
                    return item.format == VK_FORMAT_B8G8R8A8_SRGB &&
                           item.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                });
            if (preferred != formats.end()) {
                return *preferred;
            }

            return formats.front();
        }

        VkPresentModeKHR choosePresentMode(std::span<const VkPresentModeKHR> presentModes) {
            if (std::ranges::contains(presentModes, VK_PRESENT_MODE_MAILBOX_KHR)) {
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }

            return VK_PRESENT_MODE_FIFO_KHR;
        }

        VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, std::uint32_t width,
                                std::uint32_t height) {
            if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
                return capabilities.currentExtent;
            }

            return VkExtent2D{
                .width = std::clamp(width, capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width),
                .height = std::clamp(height, capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height),
            };
        }

        VkCompositeAlphaFlagBitsKHR
        chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha) {
            constexpr std::array options{
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            };

            for (VkCompositeAlphaFlagBitsKHR option : options) {
                if ((supportedCompositeAlpha & option) != 0) {
                    return option;
                }
            }

            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }

        std::uint32_t chooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities) {
            std::uint32_t imageCount = capabilities.minImageCount + 1;
            if (capabilities.maxImageCount > 0) {
                imageCount = std::min(imageCount, capabilities.maxImageCount);
            }

            return imageCount;
        }

        Result<std::vector<VkImage>> getSwapchainImages(VkDevice device, VkSwapchainKHR swapchain) {
            while (true) {
                std::uint32_t count = 0;
                VkResult result = vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vulkanError("Failed to query Vulkan swapchain images", result)};
                }

                std::vector<VkImage> images(count);
                if (count == 0) {
                    return images;
                }

                result = vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
                if (result == VK_SUCCESS) {
                    images.resize(count);
                    return images;
                }

                if (result != VK_INCOMPLETE) {
                    return std::unexpected{
                        vulkanError("Failed to query Vulkan swapchain images", result)};
                }
            }
        }

        Result<VkImageView> createImageView(VkDevice device, VkImage image, VkFormat format) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = image;
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = format;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            VkImageView imageView = VK_NULL_HANDLE;
            const VkResult result = vkCreateImageView(device, &createInfo, nullptr, &imageView);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to create Vulkan swapchain image view", result)};
            }

            return imageView;
        }

        void destroyImageViews(VkDevice device, std::span<const VkImageView> imageViews) {
            for (VkImageView imageView : imageViews) {
                vkDestroyImageView(device, imageView, nullptr);
            }
        }

        Result<std::vector<VkImageView>>
        createImageViews(VkDevice device, std::span<const VkImage> images, VkFormat format) {
            std::vector<VkImageView> imageViews;
            imageViews.reserve(images.size());
            for (VkImage image : images) {
                auto imageView = createImageView(device, image, format);
                if (!imageView) {
                    destroyImageViews(device, imageViews);
                    return std::unexpected{std::move(imageView.error())};
                }
                imageViews.push_back(*imageView);
            }

            return imageViews;
        }

        Result<VkSwapchainKHR> createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device,
                                               VkSurfaceKHR surface, std::uint32_t queueFamily,
                                               const VulkanFrameLoopDesc& desc, VkFormat& format,
                                               VkExtent2D& extent,
                                               VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE) {
            auto capabilities = querySurfaceCapabilities(physicalDevice, surface);
            if (!capabilities) {
                return std::unexpected{std::move(capabilities.error())};
            }

            constexpr VkImageUsageFlags kRequiredUsage =
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if ((capabilities->supportedUsageFlags & kRequiredUsage) != kRequiredUsage) {
                return std::unexpected{vulkanError(
                    "Vulkan surface does not support transfer-destination and color-attachment "
                    "swapchain images")};
            }

            auto formats = querySurfaceFormats(physicalDevice, surface);
            if (!formats) {
                return std::unexpected{std::move(formats.error())};
            }
            if (formats->empty()) {
                return std::unexpected{vulkanError("Vulkan surface returned no supported formats")};
            }

            auto presentModes = queryPresentModes(physicalDevice, surface);
            if (!presentModes) {
                return std::unexpected{std::move(presentModes.error())};
            }
            if (presentModes->empty()) {
                return std::unexpected{vulkanError("Vulkan surface returned no present modes")};
            }

            const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(*formats);
            const VkPresentModeKHR presentMode = choosePresentMode(*presentModes);
            const VkExtent2D imageExtent = chooseExtent(*capabilities, desc.width, desc.height);
            if (imageExtent.width == 0 || imageExtent.height == 0) {
                return std::unexpected{vulkanError("Cannot create a zero-sized Vulkan swapchain")};
            }

            VkSwapchainCreateInfoKHR createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            createInfo.surface = surface;
            createInfo.minImageCount = chooseImageCount(*capabilities);
            createInfo.imageFormat = surfaceFormat.format;
            createInfo.imageColorSpace = surfaceFormat.colorSpace;
            createInfo.imageExtent = imageExtent;
            createInfo.imageArrayLayers = 1;
            createInfo.imageUsage =
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 1;
            createInfo.pQueueFamilyIndices = &queueFamily;
            createInfo.preTransform = capabilities->currentTransform;
            createInfo.compositeAlpha = chooseCompositeAlpha(capabilities->supportedCompositeAlpha);
            createInfo.presentMode = presentMode;
            createInfo.clipped = VK_TRUE;
            createInfo.oldSwapchain = oldSwapchain;

            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            const VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError("Failed to create Vulkan swapchain", result)};
            }

            format = surfaceFormat.format;
            extent = imageExtent;
            return swapchain;
        }

        Result<VkCommandPool> createCommandPool(VkDevice device, std::uint32_t queueFamily) {
            VkCommandPoolCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            createInfo.queueFamilyIndex = queueFamily;

            VkCommandPool commandPool = VK_NULL_HANDLE;
            const VkResult result = vkCreateCommandPool(device, &createInfo, nullptr, &commandPool);
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError("Failed to create Vulkan command pool", result)};
            }

            return commandPool;
        }

        Result<VkCommandBuffer> allocateCommandBuffer(VkDevice device, VkCommandPool commandPool) {
            VkCommandBufferAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocateInfo.commandPool = commandPool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            const VkResult result = vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to allocate Vulkan command buffer", result)};
            }

            return commandBuffer;
        }

        Result<VkSemaphore> createSemaphore(VkDevice device) {
            VkSemaphoreCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkSemaphore semaphore = VK_NULL_HANDLE;
            const VkResult result = vkCreateSemaphore(device, &createInfo, nullptr, &semaphore);
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError("Failed to create Vulkan semaphore", result)};
            }

            return semaphore;
        }

        Result<std::vector<VkSemaphore>> createSemaphores(VkDevice device, std::size_t count) {
            std::vector<VkSemaphore> semaphores;
            semaphores.reserve(count);

            for (std::size_t index = 0; index < count; ++index) {
                auto semaphore = createSemaphore(device);
                if (!semaphore) {
                    for (VkSemaphore created : semaphores) {
                        vkDestroySemaphore(device, created, nullptr);
                    }

                    return std::unexpected{std::move(semaphore.error())};
                }

                semaphores.push_back(*semaphore);
            }

            return semaphores;
        }

        Result<VkFence> createFence(VkDevice device,
                                    VkFenceCreateFlags flags = VK_FENCE_CREATE_SIGNALED_BIT) {
            VkFenceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            createInfo.flags = flags;

            VkFence fence = VK_NULL_HANDLE;
            const VkResult result = vkCreateFence(device, &createInfo, nullptr, &fence);
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError("Failed to create Vulkan fence", result)};
            }

            return fence;
        }

        Result<void> drainAcquiredImageSemaphore(VkDevice device, VkQueue queue,
                                                 VkSemaphore semaphore) {
            auto fence = createFence(device, 0);
            if (!fence) {
                return std::unexpected{std::move(fence.error())};
            }
            const VkFence recoveryFence = *fence;

            VkSemaphoreSubmitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfo.semaphore = semaphore;
            waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.waitSemaphoreInfoCount = 1;
            submitInfo.pWaitSemaphoreInfos = &waitInfo;

            VkResult result = vkQueueSubmit2(queue, 1, &submitInfo, recoveryFence);
            if (result != VK_SUCCESS) {
                vkDestroyFence(device, recoveryFence, nullptr);
                return std::unexpected{
                    vulkanError("Failed to submit Vulkan acquire recovery wait", result)};
            }

            result = vkWaitForFences(device, 1, &recoveryFence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device, recoveryFence, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vulkanError("Failed to wait for Vulkan acquire recovery fence", result)};
            }

            return {};
        }

        struct ImageBarrierDesc {
            VkImage image{VK_NULL_HANDLE};
            VkImageLayout oldLayout{VK_IMAGE_LAYOUT_UNDEFINED};
            VkImageLayout newLayout{VK_IMAGE_LAYOUT_UNDEFINED};
            VkPipelineStageFlags2 srcStageMask{};
            VkAccessFlags2 srcAccessMask{};
            VkPipelineStageFlags2 dstStageMask{};
            VkAccessFlags2 dstAccessMask{};
        };

        VkImageMemoryBarrier2 imageBarrier(const ImageBarrierDesc& desc) {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = desc.srcStageMask;
            barrier.srcAccessMask = desc.srcAccessMask;
            barrier.dstStageMask = desc.dstStageMask;
            barrier.dstAccessMask = desc.dstAccessMask;
            barrier.oldLayout = desc.oldLayout;
            barrier.newLayout = desc.newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = desc.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            return barrier;
        }

        Result<VulkanFrameRecordResult>
        recordClearCommandsInStartedBuffer(const VulkanFrameRecordContext& context) {
            const VkImageMemoryBarrier2 transferBarrier = imageBarrier(ImageBarrierDesc{
                .image = context.image,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = 0,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            });

            VkDependencyInfo dependencyInfo{};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = 1;
            dependencyInfo.pImageMemoryBarriers = &transferBarrier;
            vkCmdPipelineBarrier2(context.commandBuffer, &dependencyInfo);

            VkImageSubresourceRange clearRange{};
            clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearRange.baseMipLevel = 0;
            clearRange.levelCount = 1;
            clearRange.baseArrayLayer = 0;
            clearRange.layerCount = 1;
            vkCmdClearColorImage(context.commandBuffer, context.image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &context.clearColor, 1,
                                 &clearRange);

            const VkImageMemoryBarrier2 presentBarrier = imageBarrier(ImageBarrierDesc{
                .image = context.image,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = 0,
            });
            dependencyInfo.pImageMemoryBarriers = &presentBarrier;
            vkCmdPipelineBarrier2(context.commandBuffer, &dependencyInfo);

            return VulkanFrameRecordResult{
                .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            };
        }

    } // namespace

    VulkanFrameLoop::VulkanFrameLoop(VulkanFrameLoop&& other) noexcept {
        *this = std::move(other);
    }

    VulkanFrameLoop& VulkanFrameLoop::operator=(VulkanFrameLoop&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        physicalDevice_ = std::exchange(other.physicalDevice_, VK_NULL_HANDLE);
        surface_ = std::exchange(other.surface_, VK_NULL_HANDLE);
        graphicsQueue_ = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
        graphicsQueueFamily_ = std::exchange(other.graphicsQueueFamily_, 0);
        swapchain_ = std::exchange(other.swapchain_, VK_NULL_HANDLE);
        format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        extent_ = std::exchange(other.extent_, VkExtent2D{});
        targetExtent_ = std::exchange(other.targetExtent_, VkExtent2D{});
        images_ = std::move(other.images_);
        imageViews_ = std::move(other.imageViews_);
        commandPool_ = std::exchange(other.commandPool_, VK_NULL_HANDLE);
        commandBuffer_ = std::exchange(other.commandBuffer_, VK_NULL_HANDLE);
        imageAvailable_ = std::exchange(other.imageAvailable_, VK_NULL_HANDLE);
        renderFinished_ = std::exchange(other.renderFinished_, {});
        inFlight_ = std::exchange(other.inFlight_, VK_NULL_HANDLE);
        clearColor_ = other.clearColor_;
        return *this;
    }

    VulkanFrameLoop::~VulkanFrameLoop() {
        destroy();
    }

    void VulkanFrameLoop::destroy() {
        if (graphicsQueue_ != VK_NULL_HANDLE) {
            [[maybe_unused]] const VkResult idleResult = vkQueueWaitIdle(graphicsQueue_);
        }

        if (inFlight_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, inFlight_, nullptr);
        }
        for (VkSemaphore semaphore : renderFinished_) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
        if (imageAvailable_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        }
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        }
        destroyImageViews(device_, imageViews_);
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        physicalDevice_ = VK_NULL_HANDLE;
        surface_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        graphicsQueueFamily_ = 0;
        swapchain_ = VK_NULL_HANDLE;
        format_ = VK_FORMAT_UNDEFINED;
        extent_ = {};
        targetExtent_ = {};
        images_.clear();
        imageViews_.clear();
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
        imageAvailable_ = VK_NULL_HANDLE;
        renderFinished_.clear();
        inFlight_ = VK_NULL_HANDLE;
    }

    Result<VulkanFrameLoop> VulkanFrameLoop::create(const VulkanContext& context,
                                                    const VulkanFrameLoopDesc& desc) {
        if (context.surface() == VK_NULL_HANDLE) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan frame loop without a presentation surface")};
        }

        VulkanFrameLoop frameLoop;
        frameLoop.device_ = context.device();
        frameLoop.physicalDevice_ = context.physicalDevice();
        frameLoop.surface_ = context.surface();
        frameLoop.graphicsQueue_ = context.graphicsQueue();
        frameLoop.graphicsQueueFamily_ = context.graphicsQueueFamily();
        frameLoop.clearColor_ = desc.clearColor;
        frameLoop.targetExtent_ = VkExtent2D{
            .width = desc.width,
            .height = desc.height,
        };

        auto swapchain = createSwapchain(frameLoop.physicalDevice_, frameLoop.device_,
                                         frameLoop.surface_, frameLoop.graphicsQueueFamily_, desc,
                                         frameLoop.format_, frameLoop.extent_);
        if (!swapchain) {
            return std::unexpected{std::move(swapchain.error())};
        }
        frameLoop.swapchain_ = *swapchain;

        auto images = getSwapchainImages(frameLoop.device_, frameLoop.swapchain_);
        if (!images) {
            return std::unexpected{std::move(images.error())};
        }
        frameLoop.images_ = std::move(*images);

        auto imageViews = createImageViews(frameLoop.device_, frameLoop.images_, frameLoop.format_);
        if (!imageViews) {
            return std::unexpected{std::move(imageViews.error())};
        }
        frameLoop.imageViews_ = std::move(*imageViews);

        auto commandPool = createCommandPool(frameLoop.device_, frameLoop.graphicsQueueFamily_);
        if (!commandPool) {
            return std::unexpected{std::move(commandPool.error())};
        }
        frameLoop.commandPool_ = *commandPool;

        auto commandBuffer = allocateCommandBuffer(frameLoop.device_, frameLoop.commandPool_);
        if (!commandBuffer) {
            return std::unexpected{std::move(commandBuffer.error())};
        }
        frameLoop.commandBuffer_ = *commandBuffer;

        auto imageAvailable = createSemaphore(frameLoop.device_);
        if (!imageAvailable) {
            return std::unexpected{std::move(imageAvailable.error())};
        }
        frameLoop.imageAvailable_ = *imageAvailable;

        auto renderFinished = createSemaphores(frameLoop.device_, frameLoop.images_.size());
        if (!renderFinished) {
            return std::unexpected{std::move(renderFinished.error())};
        }
        frameLoop.renderFinished_ = std::move(*renderFinished);

        auto fence = createFence(frameLoop.device_);
        if (!fence) {
            return std::unexpected{std::move(fence.error())};
        }
        frameLoop.inFlight_ = *fence;

        return frameLoop;
    }

    void VulkanFrameLoop::setTargetExtent(std::uint32_t width, std::uint32_t height) {
        targetExtent_ = VkExtent2D{
            .width = width,
            .height = height,
        };
    }

    Result<VulkanFrameStatus> VulkanFrameLoop::recreate() {
        return recreateSwapchain();
    }

    Result<VulkanFrameStatus> VulkanFrameLoop::recreateSwapchain() {
        if (targetExtent_.width == 0 || targetExtent_.height == 0) {
            return VulkanFrameStatus::OutOfDate;
        }

        VkResult result = vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError(
                "Failed to wait for Vulkan frame fence before swapchain recreation", result)};
        }

        result = vkQueueWaitIdle(graphicsQueue_);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to wait for Vulkan queue before swapchain recreation", result)};
        }

        const VkSwapchainKHR oldSwapchain = swapchain_;
        VulkanFrameLoopDesc desc{
            .width = targetExtent_.width,
            .height = targetExtent_.height,
            .clearColor = clearColor_,
        };

        VkFormat newFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D newExtent{};
        auto newSwapchain =
            createSwapchain(physicalDevice_, device_, surface_, graphicsQueueFamily_, desc,
                            newFormat, newExtent, oldSwapchain);
        if (!newSwapchain) {
            destroyImageViews(device_, imageViews_);
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
            }

            swapchain_ = VK_NULL_HANDLE;
            format_ = VK_FORMAT_UNDEFINED;
            extent_ = {};
            imageViews_.clear();
            images_.clear();
            return std::unexpected{std::move(newSwapchain.error())};
        }

        auto newImages = getSwapchainImages(device_, *newSwapchain);
        if (!newImages) {
            vkDestroySwapchainKHR(device_, *newSwapchain, nullptr);
            destroyImageViews(device_, imageViews_);
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
            }

            swapchain_ = VK_NULL_HANDLE;
            format_ = VK_FORMAT_UNDEFINED;
            extent_ = {};
            imageViews_.clear();
            images_.clear();
            return std::unexpected{std::move(newImages.error())};
        }

        auto newImageViews = createImageViews(device_, *newImages, newFormat);
        if (!newImageViews) {
            vkDestroySwapchainKHR(device_, *newSwapchain, nullptr);
            destroyImageViews(device_, imageViews_);
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
            }

            swapchain_ = VK_NULL_HANDLE;
            format_ = VK_FORMAT_UNDEFINED;
            extent_ = {};
            imageViews_.clear();
            images_.clear();
            return std::unexpected{std::move(newImageViews.error())};
        }

        auto newRenderFinished = createSemaphores(device_, newImages->size());
        if (!newRenderFinished) {
            destroyImageViews(device_, *newImageViews);
            vkDestroySwapchainKHR(device_, *newSwapchain, nullptr);
            destroyImageViews(device_, imageViews_);
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
            }

            swapchain_ = VK_NULL_HANDLE;
            format_ = VK_FORMAT_UNDEFINED;
            extent_ = {};
            imageViews_.clear();
            images_.clear();
            for (VkSemaphore semaphore : renderFinished_) {
                vkDestroySemaphore(device_, semaphore, nullptr);
            }
            renderFinished_.clear();
            return std::unexpected{std::move(newRenderFinished.error())};
        }

        for (VkSemaphore semaphore : renderFinished_) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
        destroyImageViews(device_, imageViews_);

        swapchain_ = *newSwapchain;
        format_ = newFormat;
        extent_ = newExtent;
        images_ = std::move(*newImages);
        imageViews_ = std::move(*newImageViews);
        renderFinished_ = std::move(*newRenderFinished);

        if (oldSwapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
        }

        return VulkanFrameStatus::Recreated;
    }

    Result<void> VulkanFrameLoop::recoverAcquiredImageSemaphore() {
        return drainAcquiredImageSemaphore(device_, graphicsQueue_, imageAvailable_);
    }

    Result<VulkanFrameLoop::FrameAcquireResult> VulkanFrameLoop::acquireNextImage() {
        VkResult result = vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to wait for Vulkan frame fence", result)};
        }

        std::uint32_t imageIndex = 0;
        result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_,
                                       VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            auto recreated = recreateSwapchain();
            if (!recreated) {
                return std::unexpected{std::move(recreated.error())};
            }

            return FrameAcquireResult{
                .status = *recreated,
                .hasImage = false,
                .image = {},
            };
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return std::unexpected{vulkanError("Failed to acquire Vulkan swapchain image", result)};
        }

        if (imageIndex >= renderFinished_.size()) {
            auto drained = recoverAcquiredImageSemaphore();
            if (!drained) {
                return std::unexpected{std::move(drained.error())};
            }

            return std::unexpected{
                vulkanError("Acquired Vulkan swapchain image index is out of range")};
        }

        return FrameAcquireResult{
            .status = VulkanFrameStatus::Presented,
            .hasImage = true,
            .image =
                AcquiredImage{
                    .imageIndex = imageIndex,
                    .suboptimal = result == VK_SUBOPTIMAL_KHR,
                },
        };
    }

    Result<void> VulkanFrameLoop::submitRecordedFrame(std::uint32_t imageIndex,
                                                      const VulkanFrameRecordResult& recorded) {
        VkResult result = vkResetFences(device_, 1, &inFlight_);
        if (result != VK_SUCCESS) {
            auto drained = recoverAcquiredImageSemaphore();
            if (!drained) {
                return std::unexpected{vulkanError("Failed to reset Vulkan frame fence and recover "
                                                   "the acquired image semaphore: " +
                                                       drained.error().message,
                                                   result)};
            }

            return std::unexpected{vulkanError("Failed to reset Vulkan frame fence", result)};
        }

        VkSemaphoreSubmitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = imageAvailable_;
        waitInfo.stageMask = recorded.waitStageMask == 0 ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
                                                         : recorded.waitStageMask;

        VkCommandBufferSubmitInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandInfo.commandBuffer = commandBuffer_;

        VkSemaphoreSubmitInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = renderFinished_[imageIndex];
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &signalInfo;

        result = vkQueueSubmit2(graphicsQueue_, 1, &submitInfo, inFlight_);
        if (result == VK_SUCCESS) {
            return {};
        }

        auto drained = recoverAcquiredImageSemaphore();
        auto replacementFence = createFence(device_);
        if (!replacementFence) {
            return std::unexpected{
                vulkanError("Failed to submit Vulkan frame commands and recover the frame fence: " +
                                replacementFence.error().message,
                            result)};
        }

        vkDestroyFence(device_, inFlight_, nullptr);
        inFlight_ = *replacementFence;
        if (!drained) {
            return std::unexpected{vulkanError("Failed to submit Vulkan frame commands and recover "
                                               "the acquired image semaphore: " +
                                                   drained.error().message,
                                               result)};
        }

        return std::unexpected{vulkanError("Failed to submit Vulkan frame commands", result)};
    }

    Result<VulkanFrameStatus> VulkanFrameLoop::presentFrame(std::uint32_t imageIndex,
                                                            bool acquiredSuboptimal) {
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &imageIndex;

        const VkResult result = vkQueuePresentKHR(graphicsQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return recreateSwapchain();
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return std::unexpected{vulkanError("Failed to present Vulkan swapchain image", result)};
        }
        if (acquiredSuboptimal || result == VK_SUBOPTIMAL_KHR) {
            auto recreated = recreateSwapchain();
            if (!recreated) {
                return std::unexpected{std::move(recreated.error())};
            }

            return VulkanFrameStatus::Suboptimal;
        }

        return VulkanFrameStatus::Presented;
    }

    Result<VulkanFrameRecordResult>
    VulkanFrameLoop::recordFrameCommands(std::uint32_t imageIndex,
                                         const VulkanFrameRecordCallback& record) {
        if (!record) {
            return std::unexpected{vulkanError("Cannot record a Vulkan frame without a callback")};
        }

        const VkResult resetResult = vkResetCommandBuffer(commandBuffer_, 0);
        if (resetResult != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to reset Vulkan command buffer", resetResult)};
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult result = vkBeginCommandBuffer(commandBuffer_, &beginInfo);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to begin Vulkan command buffer", result)};
        }

        auto recorded = record(VulkanFrameRecordContext{
            .commandBuffer = commandBuffer_,
            .image = images_[imageIndex],
            .imageView = imageViews_[imageIndex],
            .imageIndex = imageIndex,
            .format = format_,
            .extent = extent_,
            .clearColor = clearColor_,
        });
        if (!recorded) {
            [[maybe_unused]] const VkResult resetAfterRecordFailure =
                vkResetCommandBuffer(commandBuffer_, 0);
            return std::unexpected{std::move(recorded.error())};
        }

        result = vkEndCommandBuffer(commandBuffer_);
        if (result != VK_SUCCESS) {
            [[maybe_unused]] const VkResult resetAfterEndFailure =
                vkResetCommandBuffer(commandBuffer_, 0);
            return std::unexpected{vulkanError("Failed to end Vulkan command buffer", result)};
        }

        return *recorded;
    }

    Result<VulkanFrameRecordResult> VulkanFrameLoop::recordClearCommands(std::uint32_t imageIndex) {
        return recordFrameCommands(imageIndex, recordClearCommandsInStartedBuffer);
    }

    Result<VulkanFrameStatus> VulkanFrameLoop::renderFrame() {
        return renderFrame(recordClearCommandsInStartedBuffer);
    }

    Result<VulkanFrameStatus>
    VulkanFrameLoop::renderFrame(const VulkanFrameRecordCallback& record) {
        if (swapchain_ == VK_NULL_HANDLE) {
            return recreateSwapchain();
        }

        auto acquired = acquireNextImage();
        if (!acquired) {
            return std::unexpected{std::move(acquired.error())};
        }
        if (!acquired->hasImage) {
            return acquired->status;
        }

        const AcquiredImage image = acquired->image;
        auto recorded = recordFrameCommands(image.imageIndex, record);
        if (!recorded) {
            auto drained = recoverAcquiredImageSemaphore();
            if (!drained) {
                return std::unexpected{std::move(drained.error())};
            }

            return std::unexpected{std::move(recorded.error())};
        }

        auto submitted = submitRecordedFrame(image.imageIndex, *recorded);
        if (!submitted) {
            return std::unexpected{std::move(submitted.error())};
        }

        return presentFrame(image.imageIndex, image.suboptimal);
    }

    VkFormat VulkanFrameLoop::format() const {
        return format_;
    }

    VkExtent2D VulkanFrameLoop::extent() const {
        return extent_;
    }

} // namespace vke
