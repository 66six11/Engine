#pragma once

#include <vulkan/vulkan.h>

#include "vke/rendergraph/render_graph.hpp"

namespace vke {

    struct VulkanRenderGraphImageUsage {
        VkPipelineStageFlags2 stageMask{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 accessMask{};
    };

    struct VulkanRenderGraphImageTransition {
        RenderGraphImageHandle image{};
        VkImageLayout oldLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImageLayout newLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkPipelineStageFlags2 srcStageMask{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 srcAccessMask{};
        VkPipelineStageFlags2 dstStageMask{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 dstAccessMask{};
    };

    [[nodiscard]] inline VkFormat vulkanFormat(RenderGraphImageFormat format) {
        switch (format) {
        case RenderGraphImageFormat::B8G8R8A8Srgb:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case RenderGraphImageFormat::Undefined:
        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    [[nodiscard]] inline VkExtent2D vulkanExtent(RenderGraphExtent2D extent) {
        return VkExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    [[nodiscard]] inline VkImageLayout vulkanImageLayout(RenderGraphImageState state) {
        switch (state) {
        case RenderGraphImageState::ColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RenderGraphImageState::TransferDst:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RenderGraphImageState::Present:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case RenderGraphImageState::Undefined:
        default:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    [[nodiscard]] inline VulkanRenderGraphImageUsage vulkanImageUsage(
        RenderGraphImageState state) {
        switch (state) {
        case RenderGraphImageState::ColorAttachment:
            return VulkanRenderGraphImageUsage{
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            };
        case RenderGraphImageState::TransferDst:
            return VulkanRenderGraphImageUsage{
                .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            };
        case RenderGraphImageState::Present:
        case RenderGraphImageState::Undefined:
        default:
            return {};
        }
    }

    [[nodiscard]] inline VulkanRenderGraphImageTransition vulkanImageTransition(
        const RenderGraphImageTransition& transition) {
        const VulkanRenderGraphImageUsage srcUsage = vulkanImageUsage(transition.oldState);
        const VulkanRenderGraphImageUsage dstUsage = vulkanImageUsage(transition.newState);
        return VulkanRenderGraphImageTransition{
            .image = transition.image,
            .oldLayout = vulkanImageLayout(transition.oldState),
            .newLayout = vulkanImageLayout(transition.newState),
            .srcStageMask = srcUsage.stageMask,
            .srcAccessMask = srcUsage.accessMask,
            .dstStageMask = dstUsage.stageMask,
            .dstAccessMask = dstUsage.accessMask,
        };
    }

    [[nodiscard]] inline VkImageMemoryBarrier2 vulkanImageBarrier(
        const VulkanRenderGraphImageTransition& transition, VkImage image,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT) {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = transition.srcStageMask;
        barrier.srcAccessMask = transition.srcAccessMask;
        barrier.dstStageMask = transition.dstStageMask;
        barrier.dstAccessMask = transition.dstAccessMask;
        barrier.oldLayout = transition.oldLayout;
        barrier.newLayout = transition.newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        return barrier;
    }

    [[nodiscard]] inline VkImageMemoryBarrier2 vulkanImageBarrier(
        const RenderGraphImageTransition& transition, VkImage image,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT) {
        return vulkanImageBarrier(vulkanImageTransition(transition), image, aspectMask);
    }

} // namespace vke
