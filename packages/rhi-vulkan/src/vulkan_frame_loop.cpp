#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "vke/core/error.hpp"

namespace vke {
    namespace {

        Error vkError(std::string message, VkResult result = VK_ERROR_UNKNOWN) {
            if (result != VK_SUCCESS) {
                message += ": ";
                message += vkResultName(result);
            }

            return Error{ErrorDomain::Vulkan, static_cast<int>(result), std::move(message)};
        }

        Result<VkSurfaceCapabilitiesKHR> querySurfaceCapabilities(VkPhysicalDevice physicalDevice,
                                                                  VkSurfaceKHR surface) {
            VkSurfaceCapabilitiesKHR capabilities{};
            const VkResult result =
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
            if (result != VK_SUCCESS) {
                return std::unexpected{
                    vkError("Failed to query Vulkan surface capabilities", result)};
            }

            return capabilities;
        }

        Result<std::vector<VkSurfaceFormatKHR>> querySurfaceFormats(VkPhysicalDevice physicalDevice,
                                                                    VkSurfaceKHR surface) {
            std::uint32_t count = 0;
            VkResult result =
                vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
            if (result != VK_SUCCESS) {
                return std::unexpected{vkError("Failed to query Vulkan surface formats", result)};
            }

            std::vector<VkSurfaceFormatKHR> formats(count);
            if (count > 0) {
                result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count,
                                                              formats.data());
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vkError("Failed to query Vulkan surface formats", result)};
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
                    vkError("Failed to query Vulkan surface present modes", result)};
            }

            std::vector<VkPresentModeKHR> presentModes(count);
            if (count > 0) {
                result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count,
                                                                   presentModes.data());
                if (result != VK_SUCCESS) {
                    return std::unexpected{
                        vkError("Failed to query Vulkan surface present modes", result)};
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
                        vkError("Failed to query Vulkan swapchain images", result)};
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
                        vkError("Failed to query Vulkan swapchain images", result)};
                }
            }
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

            if ((capabilities->supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
                return std::unexpected{vkError(
                    "Vulkan surface does not support transfer-destination swapchain images")};
            }

            auto formats = querySurfaceFormats(physicalDevice, surface);
            if (!formats) {
                return std::unexpected{std::move(formats.error())};
            }
            if (formats->empty()) {
                return std::unexpected{vkError("Vulkan surface returned no supported formats")};
            }

            auto presentModes = queryPresentModes(physicalDevice, surface);
            if (!presentModes) {
                return std::unexpected{std::move(presentModes.error())};
            }
            if (presentModes->empty()) {
                return std::unexpected{vkError("Vulkan surface returned no present modes")};
            }

            const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(*formats);
            const VkPresentModeKHR presentMode = choosePresentMode(*presentModes);
            const VkExtent2D imageExtent = chooseExtent(*capabilities, desc.width, desc.height);
            if (imageExtent.width == 0 || imageExtent.height == 0) {
                return std::unexpected{vkError("Cannot create a zero-sized Vulkan swapchain")};
            }

            VkSwapchainCreateInfoKHR createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            createInfo.surface = surface;
            createInfo.minImageCount = chooseImageCount(*capabilities);
            createInfo.imageFormat = surfaceFormat.format;
            createInfo.imageColorSpace = surfaceFormat.colorSpace;
            createInfo.imageExtent = imageExtent;
            createInfo.imageArrayLayers = 1;
            createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
                return std::unexpected{vkError("Failed to create Vulkan swapchain", result)};
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
                return std::unexpected{vkError("Failed to create Vulkan command pool", result)};
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
                return std::unexpected{vkError("Failed to allocate Vulkan command buffer", result)};
            }

            return commandBuffer;
        }

        Result<VkSemaphore> createSemaphore(VkDevice device) {
            VkSemaphoreCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkSemaphore semaphore = VK_NULL_HANDLE;
            const VkResult result = vkCreateSemaphore(device, &createInfo, nullptr, &semaphore);
            if (result != VK_SUCCESS) {
                return std::unexpected{vkError("Failed to create Vulkan semaphore", result)};
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

        Result<VkFence> createFence(VkDevice device) {
            VkFenceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VkFence fence = VK_NULL_HANDLE;
            const VkResult result = vkCreateFence(device, &createInfo, nullptr, &fence);
            if (result != VK_SUCCESS) {
                return std::unexpected{vkError("Failed to create Vulkan fence", result)};
            }

            return fence;
        }

        VkImageMemoryBarrier2
        imageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask,
                     VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask) {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = srcStageMask;
            barrier.srcAccessMask = srcAccessMask;
            barrier.dstStageMask = dstStageMask;
            barrier.dstAccessMask = dstAccessMask;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            return barrier;
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
                vkError("Cannot create a Vulkan frame loop without a presentation surface")};
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

    Result<VulkanFrameStatus> VulkanFrameLoop::recreateSwapchain() {
        if (targetExtent_.width == 0 || targetExtent_.height == 0) {
            return VulkanFrameStatus::OutOfDate;
        }

        VkResult result = vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vkError("Failed to wait for Vulkan frame fence before swapchain recreation", result)};
        }

        result = vkQueueWaitIdle(graphicsQueue_);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vkError("Failed to wait for Vulkan queue before swapchain recreation", result)};
        }

        const VkSwapchainKHR oldSwapchain = swapchain_;
        VulkanFrameLoopDesc desc{
            .width = targetExtent_.width,
            .height = targetExtent_.height,
            .clearColor = clearColor_,
        };

        VkFormat newFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D newExtent{};
        auto newSwapchain = createSwapchain(physicalDevice_, device_, surface_, graphicsQueueFamily_,
                                            desc, newFormat, newExtent, oldSwapchain);
        if (!newSwapchain) {
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
            }

            swapchain_ = VK_NULL_HANDLE;
            format_ = VK_FORMAT_UNDEFINED;
            extent_ = {};
            images_.clear();
            return std::unexpected{std::move(newSwapchain.error())};
        }

        auto newImages = getSwapchainImages(device_, *newSwapchain);
        if (!newImages) {
            vkDestroySwapchainKHR(device_, *newSwapchain, nullptr);
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
            }

            swapchain_ = VK_NULL_HANDLE;
            format_ = VK_FORMAT_UNDEFINED;
            extent_ = {};
            images_.clear();
            return std::unexpected{std::move(newImages.error())};
        }

        auto newRenderFinished = createSemaphores(device_, newImages->size());
        if (!newRenderFinished) {
            vkDestroySwapchainKHR(device_, *newSwapchain, nullptr);
            if (oldSwapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
            }

            swapchain_ = VK_NULL_HANDLE;
            format_ = VK_FORMAT_UNDEFINED;
            extent_ = {};
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

        swapchain_ = *newSwapchain;
        format_ = newFormat;
        extent_ = newExtent;
        images_ = std::move(*newImages);
        renderFinished_ = std::move(*newRenderFinished);

        if (oldSwapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
        }

        return VulkanFrameStatus::Recreated;
    }

    Result<void> VulkanFrameLoop::recordClearCommands(std::uint32_t imageIndex) {
        const VkResult resetResult = vkResetCommandBuffer(commandBuffer_, 0);
        if (resetResult != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to reset Vulkan command buffer", resetResult)};
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult result = vkBeginCommandBuffer(commandBuffer_, &beginInfo);
        if (result != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to begin Vulkan command buffer", result)};
        }

        const VkImage image = images_[imageIndex];
        const VkImageMemoryBarrier2 transferBarrier =
            imageBarrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &transferBarrier;
        vkCmdPipelineBarrier2(commandBuffer_, &dependencyInfo);

        VkImageSubresourceRange clearRange{};
        clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearRange.baseMipLevel = 0;
        clearRange.levelCount = 1;
        clearRange.baseArrayLayer = 0;
        clearRange.layerCount = 1;
        vkCmdClearColorImage(commandBuffer_, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clearColor_, 1, &clearRange);

        const VkImageMemoryBarrier2 presentBarrier =
            imageBarrier(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
        dependencyInfo.pImageMemoryBarriers = &presentBarrier;
        vkCmdPipelineBarrier2(commandBuffer_, &dependencyInfo);

        result = vkEndCommandBuffer(commandBuffer_);
        if (result != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to end Vulkan command buffer", result)};
        }

        return {};
    }

    Result<VulkanFrameStatus> VulkanFrameLoop::renderFrame() {
        if (swapchain_ == VK_NULL_HANDLE) {
            return recreateSwapchain();
        }

        VkResult result = vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to wait for Vulkan frame fence", result)};
        }

        std::uint32_t imageIndex = 0;
        result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_,
                                       VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return recreateSwapchain();
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return std::unexpected{vkError("Failed to acquire Vulkan swapchain image", result)};
        }
        const bool acquiredSuboptimal = result == VK_SUBOPTIMAL_KHR;
        if (imageIndex >= renderFinished_.size()) {
            return std::unexpected{vkError("Acquired Vulkan swapchain image index is out of range")};
        }

        result = vkResetFences(device_, 1, &inFlight_);
        if (result != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to reset Vulkan frame fence", result)};
        }

        auto recorded = recordClearCommands(imageIndex);
        if (!recorded) {
            return std::unexpected{std::move(recorded.error())};
        }

        VkSemaphoreSubmitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = imageAvailable_;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

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
        if (result != VK_SUCCESS) {
            return std::unexpected{vkError("Failed to submit Vulkan frame commands", result)};
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(graphicsQueue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return recreateSwapchain();
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return std::unexpected{vkError("Failed to present Vulkan swapchain image", result)};
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

    VkFormat VulkanFrameLoop::format() const {
        return format_;
    }

    VkExtent2D VulkanFrameLoop::extent() const {
        return extent_;
    }

} // namespace vke
