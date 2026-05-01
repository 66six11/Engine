#pragma once

#include <vulkan/vulkan.h>

#include <expected>
#include <span>
#include <utility>

#include "vke/core/error.hpp"
#include "vke/renderer_basic/clear_frame_graph.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan_rendergraph/vulkan_render_graph.hpp"

namespace vke {

    struct VulkanRenderGraphImageBinding {
        RenderGraphImageHandle image{};
        VkImage vulkanImage{VK_NULL_HANDLE};
    };

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

    [[nodiscard]] inline RenderGraphImageDesc
    basicBackbufferDesc(const VulkanFrameRecordContext& frame) {
        return backbufferDesc(basicRenderGraphImageFormat(frame.format),
                              basicRenderGraphExtent(frame.extent));
    }

    [[nodiscard]] inline VulkanRenderGraphImageBinding
    basicBackbufferBinding(RenderGraphImageHandle image, const VulkanFrameRecordContext& frame) {
        return VulkanRenderGraphImageBinding{
            .image = image,
            .vulkanImage = frame.image,
        };
    }

    [[nodiscard]] inline Result<VkImage>
    findVulkanRenderGraphImage(RenderGraphImageHandle image,
                               std::span<const VulkanRenderGraphImageBinding> bindings) {
        for (const VulkanRenderGraphImageBinding& binding : bindings) {
            if (binding.image == image && binding.vulkanImage != VK_NULL_HANDLE) {
                return binding.vulkanImage;
            }
        }

        return std::unexpected{Error{
            ErrorDomain::RenderGraph,
            0,
            "RenderGraph image is not bound to a Vulkan image.",
        }};
    }

    [[nodiscard]] inline Result<void>
    recordRenderGraphImageBarrier(VkCommandBuffer commandBuffer,
                                  const RenderGraphImageTransition& transition,
                                  std::span<const VulkanRenderGraphImageBinding> bindings) {
        auto image = findVulkanRenderGraphImage(transition.image, bindings);
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }

        const VkImageMemoryBarrier2 barrier = vulkanImageBarrier(transition, *image);
        VkDependencyInfo dependencyInfo{};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
        return {};
    }

    [[nodiscard]] inline Result<void>
    recordRenderGraphTransitions(const VulkanFrameRecordContext& frame,
                                 std::span<const RenderGraphImageTransition> transitions,
                                 std::span<const VulkanRenderGraphImageBinding> bindings) {
        for (const RenderGraphImageTransition& transition : transitions) {
            auto recorded =
                recordRenderGraphImageBarrier(frame.commandBuffer, transition, bindings);
            if (!recorded) {
                return std::unexpected{std::move(recorded.error())};
            }
        }

        return {};
    }

} // namespace vke
