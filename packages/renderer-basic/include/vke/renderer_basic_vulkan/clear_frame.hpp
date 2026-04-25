#pragma once

#include <expected>
#include <utility>

#include <vulkan/vulkan.h>

#include "vke/core/result.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/renderer_basic/clear_frame_graph.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan/vulkan_render_graph.hpp"

namespace vke {
    namespace detail {

        [[nodiscard]] inline RenderGraphImageFormat renderGraphImageFormat(VkFormat format) {
            switch (format) {
            case VK_FORMAT_B8G8R8A8_SRGB:
                return RenderGraphImageFormat::B8G8R8A8Srgb;
            default:
                return RenderGraphImageFormat::Undefined;
            }
        }

        [[nodiscard]] inline RenderGraphExtent2D renderGraphExtent(VkExtent2D extent) {
            return RenderGraphExtent2D{
                .width = extent.width,
                .height = extent.height,
            };
        }

        [[nodiscard]] inline RenderGraphImageDesc backbufferDesc(
            const VulkanFrameRecordContext& frame) {
            return vke::backbufferDesc(renderGraphImageFormat(frame.format),
                                       renderGraphExtent(frame.extent));
        }

        inline void recordImageBarrier(VkCommandBuffer commandBuffer,
                                       const RenderGraphImageTransition& transition,
                                       VkImage image) {
            const VkImageMemoryBarrier2 barrier = vulkanImageBarrier(transition, image);
            VkDependencyInfo dependencyInfo{};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = 1;
            dependencyInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
        }

    } // namespace detail

    [[nodiscard]] inline Result<VulkanFrameRecordResult> recordBasicClearFrame(
        const VulkanFrameRecordContext& frame) {
        RenderGraph graph;
        const auto backbuffer = graph.importImage(detail::backbufferDesc(frame));

        graph.addPass("ClearColor")
            .writeTransfer(backbuffer)
            .execute([&frame](RenderGraphPassContext pass) -> Result<void> {
                for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                    detail::recordImageBarrier(frame.commandBuffer, transition, frame.image);
                }

                VkImageSubresourceRange clearRange{};
                clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                clearRange.baseMipLevel = 0;
                clearRange.levelCount = 1;
                clearRange.baseArrayLayer = 0;
                clearRange.layerCount = 1;
                vkCmdClearColorImage(frame.commandBuffer, frame.image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &frame.clearColor, 1,
                                     &clearRange);
                return {};
            });

        auto compiled = graph.compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        auto executed = graph.execute(*compiled);
        if (!executed) {
            return std::unexpected{std::move(executed.error())};
        }

        for (const RenderGraphImageTransition& transition : compiled->finalTransitions) {
            detail::recordImageBarrier(frame.commandBuffer, transition, frame.image);
        }

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }

    [[nodiscard]] inline Result<VulkanFrameRecordResult> recordBasicDynamicClearFrame(
        const VulkanFrameRecordContext& frame) {
        RenderGraph graph;
        const auto backbuffer = graph.importImage(detail::backbufferDesc(frame));

        graph.addPass("DynamicClearColor")
            .writeColor(backbuffer)
            .execute([&frame](RenderGraphPassContext pass) -> Result<void> {
                for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                    detail::recordImageBarrier(frame.commandBuffer, transition, frame.image);
                }

                VkRenderingAttachmentInfo colorAttachment{};
                colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colorAttachment.imageView = frame.imageView;
                colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAttachment.clearValue = VkClearValue{
                    .color = frame.clearColor,
                };

                VkRenderingInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                renderingInfo.renderArea = VkRect2D{
                    .offset = VkOffset2D{.x = 0, .y = 0},
                    .extent = frame.extent,
                };
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachments = &colorAttachment;

                vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
                vkCmdEndRendering(frame.commandBuffer);
                return {};
            });

        auto compiled = graph.compile();
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }

        auto executed = graph.execute(*compiled);
        if (!executed) {
            return std::unexpected{std::move(executed.error())};
        }

        for (const RenderGraphImageTransition& transition : compiled->finalTransitions) {
            detail::recordImageBarrier(frame.commandBuffer, transition, frame.image);
        }

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    }

} // namespace vke
