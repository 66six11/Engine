#pragma once

#include <expected>
#include <utility>

#include <vulkan/vulkan.h>

#include "vke/core/result.hpp"
#include "vke/rendergraph/render_graph.hpp"
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

    [[nodiscard]] inline Result<void> recordBasicClearFrame(
        const VulkanFrameRecordContext& frame) {
        RenderGraph graph;
        const auto backbuffer = graph.importImage(RenderGraphImageDesc{
            .name = "Backbuffer",
            .format = detail::renderGraphImageFormat(frame.format),
            .extent = RenderGraphExtent2D{
                .width = frame.extent.width,
                .height = frame.extent.height,
            },
            .initialState = RenderGraphImageState::Undefined,
            .finalState = RenderGraphImageState::Present,
        });

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

        return {};
    }

} // namespace vke
