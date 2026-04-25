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

} // namespace vke
