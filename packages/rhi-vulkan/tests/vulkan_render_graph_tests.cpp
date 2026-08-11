#include <vulkan/vulkan.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

#include "asharia/rendergraph/render_graph_types.hpp"
#include "asharia/rhi_vulkan_rendergraph/vulkan_render_graph.hpp"

namespace {

    [[nodiscard]] bool mapsColorReadWriteUsage() {
        const asharia::VulkanRenderGraphImageUsage usage =
            asharia::vulkanImageUsage(asharia::RenderGraphImageState::ColorReadWrite);
        return asharia::vulkanImageLayout(asharia::RenderGraphImageState::ColorReadWrite) ==
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               usage.stageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT &&
               usage.accessMask == (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }

    [[nodiscard]] bool mapsColorWriteToReadWriteTransition() {
        const asharia::VulkanRenderGraphImageTransition transition =
            asharia::vulkanImageTransition(asharia::RenderGraphImageTransition{
                .image = {},
                .imageName = {},
                .oldState = asharia::RenderGraphImageState::ColorAttachment,
                .oldShaderStage = asharia::RenderGraphShaderStage::None,
                .newState = asharia::RenderGraphImageState::ColorReadWrite,
                .newShaderStage = asharia::RenderGraphShaderStage::None,
            });
        return transition.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               transition.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               transition.srcStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT &&
               transition.srcAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT &&
               transition.dstStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT &&
               transition.dstAccessMask == (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }

    [[nodiscard]] bool mapsRepeatedColorReadWriteTransition() {
        const asharia::VulkanRenderGraphImageTransition transition =
            asharia::vulkanImageTransition(asharia::RenderGraphImageTransition{
                .image = {},
                .imageName = {},
                .oldState = asharia::RenderGraphImageState::ColorReadWrite,
                .oldShaderStage = asharia::RenderGraphShaderStage::None,
                .newState = asharia::RenderGraphImageState::ColorReadWrite,
                .newShaderStage = asharia::RenderGraphShaderStage::None,
            });
        const VkAccessFlags2 expectedAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        return transition.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               transition.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               transition.srcStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT &&
               transition.srcAccessMask == expectedAccess &&
               transition.dstStageMask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT &&
               transition.dstAccessMask == expectedAccess;
    }

    [[nodiscard]] bool mapsVertexReadUsage() {
        const asharia::VulkanRenderGraphBufferUsage usage =
            asharia::vulkanBufferUsage(asharia::RenderGraphBufferState::VertexRead);
        return usage.stageMask == VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT &&
               usage.accessMask == VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    }

    [[nodiscard]] bool mapsIndexReadUsage() {
        const asharia::VulkanRenderGraphBufferUsage usage =
            asharia::vulkanBufferUsage(asharia::RenderGraphBufferState::IndexRead);
        return usage.stageMask == VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT &&
               usage.accessMask == VK_ACCESS_2_INDEX_READ_BIT;
    }

    [[nodiscard]] bool mapsTransferWriteToVertexReadTransition() {
        const asharia::VulkanRenderGraphBufferTransition transition =
            asharia::vulkanBufferTransition(asharia::RenderGraphBufferTransition{
                .bufferName = {},
                .oldState = asharia::RenderGraphBufferState::TransferWrite,
                .newState = asharia::RenderGraphBufferState::VertexRead,
            });
        return transition.srcStageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT &&
               transition.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT &&
               transition.dstStageMask == VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT &&
               transition.dstAccessMask == VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    }

    [[nodiscard]] bool mapsTransferWriteToIndexReadTransition() {
        const asharia::VulkanRenderGraphBufferTransition transition =
            asharia::vulkanBufferTransition(asharia::RenderGraphBufferTransition{
                .bufferName = {},
                .oldState = asharia::RenderGraphBufferState::TransferWrite,
                .newState = asharia::RenderGraphBufferState::IndexRead,
            });
        return transition.srcStageMask == VK_PIPELINE_STAGE_2_TRANSFER_BIT &&
               transition.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT &&
               transition.dstStageMask == VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT &&
               transition.dstAccessMask == VK_ACCESS_2_INDEX_READ_BIT;
    }

} // namespace

// Unexpected allocation failures are converted to process failure by the runtime.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    using Test = bool (*)();
    const std::array tests{
        std::pair<std::string_view, Test>{"mapsColorReadWriteUsage", mapsColorReadWriteUsage},
        std::pair<std::string_view, Test>{"mapsColorWriteToReadWriteTransition",
                                          mapsColorWriteToReadWriteTransition},
        std::pair<std::string_view, Test>{"mapsRepeatedColorReadWriteTransition",
                                          mapsRepeatedColorReadWriteTransition},
        std::pair<std::string_view, Test>{"mapsVertexReadUsage", mapsVertexReadUsage},
        std::pair<std::string_view, Test>{"mapsIndexReadUsage", mapsIndexReadUsage},
        std::pair<std::string_view, Test>{"mapsTransferWriteToVertexReadTransition",
                                          mapsTransferWriteToVertexReadTransition},
        std::pair<std::string_view, Test>{"mapsTransferWriteToIndexReadTransition",
                                          mapsTransferWriteToIndexReadTransition},
    };

    for (const auto& [name, test] : tests) {
        if (!test()) {
            std::cerr << name << " failed.\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << tests.size() << " Vulkan render graph tests passed.\n";
    return EXIT_SUCCESS;
}
