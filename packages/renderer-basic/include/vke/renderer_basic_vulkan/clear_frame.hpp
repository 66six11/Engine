#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <expected>
#include <utility>

#include "vke/core/result.hpp"
#include "vke/renderer_basic_vulkan/frame_graph_vulkan.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"

namespace vke {
    [[nodiscard]] inline Result<VulkanFrameRecordResult>
    recordBasicClearFrame(const VulkanFrameRecordContext& frame) {
        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const std::array bindings{basicBackbufferBinding(backbuffer, frame)};

        graph.addPass("ClearColor")
            .writeTransfer("target", backbuffer)
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
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

        auto finalTransitions =
            recordRenderGraphTransitions(frame, compiled->finalTransitions, bindings);
        if (!finalTransitions) {
            return std::unexpected{std::move(finalTransitions.error())};
        }

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }

    [[nodiscard]] inline Result<VulkanFrameRecordResult>
    recordBasicDynamicClearFrame(const VulkanFrameRecordContext& frame) {
        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const std::array bindings{basicBackbufferBinding(backbuffer, frame)};

        graph.addPass("DynamicClearColor")
            .writeColor("target", backbuffer)
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
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

        auto finalTransitions =
            recordRenderGraphTransitions(frame, compiled->finalTransitions, bindings);
        if (!finalTransitions) {
            return std::unexpected{std::move(finalTransitions.error())};
        }

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    }

} // namespace vke
