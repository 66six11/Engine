#pragma once

#include <vulkan/vulkan.h>

#include "asharia/rendergraph/render_graph.hpp"

namespace asharia {

    struct VulkanRenderGraphImageUsage {
        VkPipelineStageFlags2 stageMask{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 accessMask{};
    };

    struct VulkanRenderGraphBufferUsage {
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

    struct VulkanRenderGraphBufferTransition {
        RenderGraphBufferHandle buffer{};
        VkPipelineStageFlags2 srcStageMask{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 srcAccessMask{};
        VkPipelineStageFlags2 dstStageMask{VK_PIPELINE_STAGE_2_NONE};
        VkAccessFlags2 dstAccessMask{};
    };

    [[nodiscard]] inline VkFormat vulkanFormat(RenderGraphImageFormat format) {
        switch (format) {
        case RenderGraphImageFormat::Undefined:
            return VK_FORMAT_UNDEFINED;
        case RenderGraphImageFormat::B8G8R8A8Srgb:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case RenderGraphImageFormat::D32Sfloat:
            return VK_FORMAT_D32_SFLOAT;
        }
        return VK_FORMAT_UNDEFINED;
    }

    [[nodiscard]] inline VkExtent2D vulkanExtent(RenderGraphExtent2D extent) {
        return VkExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    [[nodiscard]] inline VkImageLayout vulkanImageLayout(RenderGraphImageState state) {
        switch (state) {
        case RenderGraphImageState::Undefined:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        case RenderGraphImageState::ColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RenderGraphImageState::ShaderRead:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RenderGraphImageState::DepthAttachmentRead:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        case RenderGraphImageState::DepthAttachmentWrite:
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case RenderGraphImageState::DepthSampledRead:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RenderGraphImageState::TransferDst:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RenderGraphImageState::Present:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }

    [[nodiscard]] inline VkPipelineStageFlags2
    vulkanShaderStage(RenderGraphShaderStage shaderStage) {
        switch (shaderStage) {
        case RenderGraphShaderStage::Fragment:
            return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        case RenderGraphShaderStage::Compute:
            return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case RenderGraphShaderStage::None:
        default:
            return VK_PIPELINE_STAGE_2_NONE;
        }
    }

    [[nodiscard]] inline VulkanRenderGraphImageUsage
    vulkanImageUsage(RenderGraphImageState state,
                     RenderGraphShaderStage shaderStage = RenderGraphShaderStage::None) {
        switch (state) {
        case RenderGraphImageState::Undefined:
        case RenderGraphImageState::Present:
            return {};
        case RenderGraphImageState::ColorAttachment:
            return VulkanRenderGraphImageUsage{
                .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            };
        case RenderGraphImageState::ShaderRead:
            return VulkanRenderGraphImageUsage{
                .stageMask = vulkanShaderStage(shaderStage),
                .accessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            };
        case RenderGraphImageState::DepthAttachmentRead:
            return VulkanRenderGraphImageUsage{
                .stageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            };
        case RenderGraphImageState::DepthAttachmentWrite:
            return VulkanRenderGraphImageUsage{
                .stageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            };
        case RenderGraphImageState::DepthSampledRead:
            return VulkanRenderGraphImageUsage{
                .stageMask = vulkanShaderStage(shaderStage),
                .accessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            };
        case RenderGraphImageState::TransferDst:
            return VulkanRenderGraphImageUsage{
                .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            };
        }
        return {};
    }

    [[nodiscard]] inline VulkanRenderGraphBufferUsage
    vulkanBufferUsage(RenderGraphBufferState state,
                      RenderGraphShaderStage shaderStage = RenderGraphShaderStage::None) {
        switch (state) {
        case RenderGraphBufferState::Undefined:
            return {};
        case RenderGraphBufferState::TransferWrite:
            return VulkanRenderGraphBufferUsage{
                .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            };
        case RenderGraphBufferState::ShaderRead:
            return VulkanRenderGraphBufferUsage{
                .stageMask = vulkanShaderStage(shaderStage),
                .accessMask = VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
                              VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            };
        case RenderGraphBufferState::StorageReadWrite:
            return VulkanRenderGraphBufferUsage{
                .stageMask = vulkanShaderStage(shaderStage),
                .accessMask =
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            };
        }
        return {};
    }

    [[nodiscard]] inline VulkanRenderGraphImageTransition
    vulkanImageTransition(const RenderGraphImageTransition& transition) {
        const VulkanRenderGraphImageUsage srcUsage =
            vulkanImageUsage(transition.oldState, transition.oldShaderStage);
        const VulkanRenderGraphImageUsage dstUsage =
            vulkanImageUsage(transition.newState, transition.newShaderStage);
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

    [[nodiscard]] inline VulkanRenderGraphBufferTransition
    vulkanBufferTransition(const RenderGraphBufferTransition& transition) {
        const VulkanRenderGraphBufferUsage srcUsage =
            vulkanBufferUsage(transition.oldState, transition.oldShaderStage);
        const VulkanRenderGraphBufferUsage dstUsage =
            vulkanBufferUsage(transition.newState, transition.newShaderStage);
        return VulkanRenderGraphBufferTransition{
            .buffer = transition.buffer,
            .srcStageMask = srcUsage.stageMask,
            .srcAccessMask = srcUsage.accessMask,
            .dstStageMask = dstUsage.stageMask,
            .dstAccessMask = dstUsage.accessMask,
        };
    }

    [[nodiscard]] inline VkImageMemoryBarrier2
    vulkanImageBarrier(const VulkanRenderGraphImageTransition& transition, VkImage image,
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

    [[nodiscard]] inline VkImageMemoryBarrier2
    vulkanImageBarrier(const RenderGraphImageTransition& transition, VkImage image,
                       VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT) {
        return vulkanImageBarrier(vulkanImageTransition(transition), image, aspectMask);
    }

    [[nodiscard]] inline VkBufferMemoryBarrier2
    vulkanBufferBarrier(const VulkanRenderGraphBufferTransition& transition, VkBuffer buffer,
                        VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) {
        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = transition.srcStageMask;
        barrier.srcAccessMask = transition.srcAccessMask;
        barrier.dstStageMask = transition.dstStageMask;
        barrier.dstAccessMask = transition.dstAccessMask;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer;
        barrier.offset = offset;
        barrier.size = size;
        return barrier;
    }

    [[nodiscard]] inline VkBufferMemoryBarrier2
    vulkanBufferBarrier(const RenderGraphBufferTransition& transition, VkBuffer buffer,
                        VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) {
        return vulkanBufferBarrier(vulkanBufferTransition(transition), buffer, offset, size);
    }

} // namespace asharia
