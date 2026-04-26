#pragma once

#include <span>

#include <vulkan/vulkan.h>

#include "vke/renderer_basic/clear_frame_graph.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan/vulkan_render_graph.hpp"

namespace vke {

    [[nodiscard]] inline RenderGraphImageFormat basicRenderGraphImageFormat(VkFormat format) {
        switch (format) {
        case VK_FORMAT_B8G8R8A8_SRGB:
            return RenderGraphImageFormat::B8G8R8A8Srgb;
        default:
            return RenderGraphImageFormat::Undefined;
        }
    }

    [[nodiscard]] inline RenderGraphExtent2D basicRenderGraphExtent(VkExtent2D extent) {
        return RenderGraphExtent2D{
            .width = extent.width,
            .height = extent.height,
        };
    }

    [[nodiscard]] inline RenderGraphImageDesc basicBackbufferDesc(
        const VulkanFrameRecordContext& frame) {
        return backbufferDesc(basicRenderGraphImageFormat(frame.format),
                              basicRenderGraphExtent(frame.extent));
    }

    inline void recordRenderGraphImageBarrier(VkCommandBuffer commandBuffer,
                                              const RenderGraphImageTransition& transition,
                                              VkImage image) {
        const VkImageMemoryBarrier2 barrier = vulkanImageBarrier(transition, image);
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    }

    inline void recordRenderGraphTransitions(
        const VulkanFrameRecordContext& frame,
        std::span<const RenderGraphImageTransition> transitions) {
        for (const RenderGraphImageTransition& transition : transitions) {
            recordRenderGraphImageBarrier(frame.commandBuffer, transition, frame.image);
        }
    }

} // namespace vke
