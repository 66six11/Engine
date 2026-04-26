#include "vke/renderer_basic_vulkan/basic_triangle_renderer.hpp"

#include <cstddef>
#include <cstring>
#include <expected>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "vke/core/error.hpp"
#include "vke/renderer_basic/clear_frame_graph.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/rhi_vulkan/vulkan_render_graph.hpp"

namespace vke {
    namespace {

        [[nodiscard]] Error shaderError(std::string message) {
            return Error{ErrorDomain::Shader, 0, std::move(message)};
        }

        [[nodiscard]] Result<std::vector<std::uint32_t>> readSpirvFile(
            const std::filesystem::path& path) {
            std::ifstream file{path, std::ios::binary | std::ios::ate};
            if (!file) {
                return std::unexpected{shaderError("Failed to open SPIR-V file: " +
                                                   path.string())};
            }

            const std::streamsize size = file.tellg();
            if (size <= 0 ||
                size % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) {
                return std::unexpected{
                    shaderError("SPIR-V file size is invalid: " + path.string())};
            }

            std::vector<char> bytes(static_cast<std::size_t>(size));
            file.seekg(0, std::ios::beg);
            if (!file.read(bytes.data(), size)) {
                return std::unexpected{
                    shaderError("Failed to read SPIR-V file: " + path.string())};
            }

            std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(words.data(), bytes.data(), bytes.size());
            return words;
        }

        [[nodiscard]] RenderGraphImageFormat renderGraphImageFormat(VkFormat format) {
            switch (format) {
            case VK_FORMAT_B8G8R8A8_SRGB:
                return RenderGraphImageFormat::B8G8R8A8Srgb;
            default:
                return RenderGraphImageFormat::Undefined;
            }
        }

        [[nodiscard]] RenderGraphExtent2D renderGraphExtent(VkExtent2D extent) {
            return RenderGraphExtent2D{
                .width = extent.width,
                .height = extent.height,
            };
        }

        [[nodiscard]] RenderGraphImageDesc backbufferDesc(
            const VulkanFrameRecordContext& frame) {
            return vke::backbufferDesc(renderGraphImageFormat(frame.format),
                                       renderGraphExtent(frame.extent));
        }

        void recordImageBarrier(VkCommandBuffer commandBuffer,
                                const RenderGraphImageTransition& transition, VkImage image) {
            const VkImageMemoryBarrier2 barrier = vulkanImageBarrier(transition, image);
            VkDependencyInfo dependencyInfo{};
            dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependencyInfo.imageMemoryBarrierCount = 1;
            dependencyInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
        }

        void recordTransitions(const VulkanFrameRecordContext& frame,
                               std::span<const RenderGraphImageTransition> transitions) {
            for (const RenderGraphImageTransition& transition : transitions) {
                recordImageBarrier(frame.commandBuffer, transition, frame.image);
            }
        }

        void recordTransferClear(const VulkanFrameRecordContext& frame) {
            VkImageSubresourceRange clearRange{};
            clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearRange.baseMipLevel = 0;
            clearRange.levelCount = 1;
            clearRange.baseArrayLayer = 0;
            clearRange.layerCount = 1;
            vkCmdClearColorImage(frame.commandBuffer, frame.image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &frame.clearColor, 1,
                                 &clearRange);
        }

        void recordTriangleDraw(const VulkanFrameRecordContext& frame, VkPipeline pipeline,
                                BasicDrawItem drawItem) {
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = frame.imageView;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

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
            vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
            vkCmdDraw(frame.commandBuffer, drawItem.vertexCount, drawItem.instanceCount,
                      drawItem.firstVertex, drawItem.firstInstance);
            vkCmdEndRendering(frame.commandBuffer);
        }

    } // namespace

    BasicTriangleRenderer::BasicTriangleRenderer(BasicTriangleRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicTriangleRenderer& BasicTriangleRenderer::operator=(
        BasicTriangleRenderer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        vertexShader_ = std::move(other.vertexShader_);
        fragmentShader_ = std::move(other.fragmentShader_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipeline_ = std::move(other.pipeline_);
        pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
        drawItem_ = std::exchange(other.drawItem_, basicTriangleDrawItem());
        return *this;
    }

    Result<BasicTriangleRenderer> BasicTriangleRenderer::create(
        const BasicTriangleRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create triangle renderer without a device"}};
        }
        if (desc.drawItem.vertexCount == 0 || desc.drawItem.instanceCount == 0) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Triangle renderer draw item must draw something"}};
        }

        auto vertexCode = readSpirvFile(desc.shaderDirectory / "basic_triangle.vert.spv");
        if (!vertexCode) {
            return std::unexpected{std::move(vertexCode.error())};
        }
        auto fragmentCode = readSpirvFile(desc.shaderDirectory / "basic_triangle.frag.spv");
        if (!fragmentCode) {
            return std::unexpected{std::move(fragmentCode.error())};
        }

        auto vertexShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
            .device = desc.device,
            .code = *vertexCode,
        });
        if (!vertexShader) {
            return std::unexpected{std::move(vertexShader.error())};
        }
        auto fragmentShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
            .device = desc.device,
            .code = *fragmentCode,
        });
        if (!fragmentShader) {
            return std::unexpected{std::move(fragmentShader.error())};
        }

        auto pipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
            .device = desc.device,
        });
        if (!pipelineLayout) {
            return std::unexpected{std::move(pipelineLayout.error())};
        }

        BasicTriangleRenderer renderer;
        renderer.device_ = desc.device;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.pipelineLayout_ = std::move(*pipelineLayout);
        renderer.drawItem_ = desc.drawItem;
        return renderer;
    }

    Result<void> BasicTriangleRenderer::ensurePipeline(VkFormat colorFormat) {
        if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat) {
            return {};
        }

        auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
            .device = device_,
            .layout = pipelineLayout_.handle(),
            .vertexShader = vertexShader_.handle(),
            .fragmentShader = fragmentShader_.handle(),
            .colorFormat = colorFormat,
        });
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        pipeline_ = std::move(*pipeline);
        pipelineFormat_ = colorFormat;
        return {};
    }

    Result<VulkanFrameRecordResult> BasicTriangleRenderer::recordFrame(
        const VulkanFrameRecordContext& frame) {
        auto pipeline = ensurePipeline(frame.format);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(backbufferDesc(frame));

        graph.addPass("ClearColor")
            .writeTransfer(backbuffer)
            .execute([&frame](RenderGraphPassContext pass) -> Result<void> {
                recordTransitions(frame, pass.transitionsBefore);
                recordTransferClear(frame);
                return {};
            });

        graph.addPass("Triangle")
            .writeColor(backbuffer)
            .execute([&frame, this](RenderGraphPassContext pass) -> Result<void> {
                recordTransitions(frame, pass.transitionsBefore);
                recordTriangleDraw(frame, pipeline_.handle(), drawItem_);
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

        recordTransitions(frame, compiled->finalTransitions);

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }

} // namespace vke
