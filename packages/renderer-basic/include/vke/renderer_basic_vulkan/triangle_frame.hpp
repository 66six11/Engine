#pragma once

#include <expected>
#include <utility>

#include <vulkan/vulkan.h>

#include "vke/core/result.hpp"
#include "vke/renderer_basic/clear_frame_graph.hpp"
#include "vke/renderer_basic/draw_item.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan/vulkan_render_graph.hpp"

namespace vke {
    namespace detail {

        [[nodiscard]] inline RenderGraphImageFormat triangleRenderGraphImageFormat(
            VkFormat format) {
            switch (format) {
            case VK_FORMAT_B8G8R8A8_SRGB:
                return RenderGraphImageFormat::B8G8R8A8Srgb;
            default:
                return RenderGraphImageFormat::Undefined;
            }
        }

        [[nodiscard]] inline RenderGraphExtent2D triangleRenderGraphExtent(VkExtent2D extent) {
            return RenderGraphExtent2D{
                .width = extent.width,
                .height = extent.height,
            };
        }

        [[nodiscard]] inline RenderGraphImageDesc triangleBackbufferDesc(
            const VulkanFrameRecordContext& frame) {
            return vke::backbufferDesc(triangleRenderGraphImageFormat(frame.format),
                                       triangleRenderGraphExtent(frame.extent));
        }

        inline void recordTriangleImageBarrier(VkCommandBuffer commandBuffer,
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

    [[nodiscard]] inline Result<VulkanFrameRecordResult> recordBasicTriangleFrame(
        const VulkanFrameRecordContext& frame, VkPipeline pipeline, VkBuffer vertexBuffer,
        BasicDrawItem drawItem = basicTriangleDrawItem()) {
        RenderGraph graph;
        const auto backbuffer = graph.importImage(detail::triangleBackbufferDesc(frame));

        graph.addPass("Triangle")
            .writeColor(backbuffer)
            .execute([&frame, pipeline, drawItem](RenderGraphPassContext pass) -> Result<void> {
                for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                    detail::recordTriangleImageBarrier(frame.commandBuffer, transition,
                                                       frame.image);
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

                const VkViewport viewport{
                    .x = 0.0F,
                    .y = 0.0F,
                    .width = static_cast<float>(frame.extent.width),
                    .height = static_cast<float>(frame.extent.height),
                    .minDepth = 0.0F,
                    .maxDepth = 1.0F,
                };
                const VkRect2D scissor{
                    .offset = VkOffset2D{.x = 0, .y = 0},
                    .extent = frame.extent,
                };

                vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
                vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                constexpr VkDeviceSize vertexBufferOffset = 0;
                vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &vertexBuffer,
                                       &vertexBufferOffset);
                vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
                vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
                vkCmdDraw(frame.commandBuffer, drawItem.vertexCount, drawItem.instanceCount,
                          drawItem.firstVertex, drawItem.firstInstance);
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
            detail::recordTriangleImageBarrier(frame.commandBuffer, transition, frame.image);
        }

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    }

} // namespace vke
