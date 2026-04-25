#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "vke/core/result.hpp"
#include "vke/rhi_vulkan/vulkan_context.hpp"

namespace vke {

    struct VulkanFrameLoopDesc {
        std::uint32_t width{1280};
        std::uint32_t height{720};
        VkClearColorValue clearColor{{0.02F, 0.04F, 0.08F, 1.0F}};
    };

    enum class VulkanFrameStatus {
        Presented,
        Suboptimal,
        OutOfDate,
    };

    class VulkanFrameLoop {
    public:
        VulkanFrameLoop() = default;
        VulkanFrameLoop(const VulkanFrameLoop&) = delete;
        VulkanFrameLoop& operator=(const VulkanFrameLoop&) = delete;
        VulkanFrameLoop(VulkanFrameLoop&& other) noexcept;
        VulkanFrameLoop& operator=(VulkanFrameLoop&& other) noexcept;
        ~VulkanFrameLoop();

        [[nodiscard]] static Result<VulkanFrameLoop> create(const VulkanContext& context,
                                                            const VulkanFrameLoopDesc& desc);

        [[nodiscard]] Result<VulkanFrameStatus> renderFrame();

        [[nodiscard]] VkFormat format() const;
        [[nodiscard]] VkExtent2D extent() const;

    private:
        void destroy();
        [[nodiscard]] Result<void> recordClearCommands(std::uint32_t imageIndex);

        VkDevice device_{VK_NULL_HANDLE};
        VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
        VkSurfaceKHR surface_{VK_NULL_HANDLE};
        VkQueue graphicsQueue_{VK_NULL_HANDLE};
        std::uint32_t graphicsQueueFamily_{0};

        VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
        VkFormat format_{VK_FORMAT_UNDEFINED};
        VkExtent2D extent_{};
        std::vector<VkImage> images_;

        VkCommandPool commandPool_{VK_NULL_HANDLE};
        VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
        VkSemaphore imageAvailable_{VK_NULL_HANDLE};
        VkSemaphore renderFinished_{VK_NULL_HANDLE};
        VkFence inFlight_{VK_NULL_HANDLE};
        VkClearColorValue clearColor_{{0.02F, 0.04F, 0.08F, 1.0F}};
    };

} // namespace vke
