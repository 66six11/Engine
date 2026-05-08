#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <expected>
#include <utility>
#include <vector>

#include "vke/core/error.hpp"
#include "vke/core/result.hpp"
#include "vke/renderer_basic/render_graph_schemas.hpp"
#include "vke/renderer_basic_vulkan/frame_graph_vulkan.hpp"
#include "vke/rendergraph/render_graph.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan/vulkan_image.hpp"

namespace vke {
    [[nodiscard]] inline Result<VulkanFrameRecordResult>
    recordBasicClearFrame(const VulkanFrameRecordContext& frame) {
        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const std::array bindings{basicBackbufferBinding(backbuffer, frame)};
        const BasicTransferClearParams clearParams{
            .color =
                {
                    frame.clearColor.float32[0],
                    frame.clearColor.float32[1],
                    frame.clearColor.float32[2],
                    frame.clearColor.float32[3],
                },
        };

        graph.addPass("ClearColor", kBasicTransferClearPassType)
            .setParams(kBasicTransferClearParamsType, clearParams)
            .writeTransfer("target", backbuffer)
            .recordCommands([clearParams](RenderGraphCommandList& commands) {
                commands.clearColor("target", clearParams.color);
            })
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                [[maybe_unused]] const auto debugLabel =
                    VulkanDebugLabelScope::begin(frame, pass.name);
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

        const RenderGraphSchemaRegistry schemas = basicRenderGraphSchemaRegistry();
        auto compiled = graph.compile(schemas);
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
        const BasicTransferClearParams clearParams{
            .color =
                {
                    frame.clearColor.float32[0],
                    frame.clearColor.float32[1],
                    frame.clearColor.float32[2],
                    frame.clearColor.float32[3],
                },
        };

        graph.addPass("DynamicClearColor", kBasicDynamicClearPassType)
            .setParams(kBasicDynamicClearParamsType, clearParams)
            .writeColor("target", backbuffer)
            .recordCommands([clearParams](RenderGraphCommandList& commands) {
                commands.clearColor("target", clearParams.color);
            })
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                [[maybe_unused]] const auto debugLabel =
                    VulkanDebugLabelScope::begin(frame, pass.name);
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

        const RenderGraphSchemaRegistry schemas = basicRenderGraphSchemaRegistry();
        auto compiled = graph.compile(schemas);
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

    class BasicTransientFrameRecorder {
    public:
        BasicTransientFrameRecorder(VkDevice device, VmaAllocator allocator)
            : device_{device}, allocator_{allocator} {}

        [[nodiscard]] VulkanTransientImagePoolStats transientPoolStats() const {
            return transientImagePool_.stats();
        }

        [[nodiscard]] Result<VulkanFrameRecordResult>
        record(const VulkanFrameRecordContext& frame) {
            auto reset = releaseTransientResources(frame);
            if (!reset) {
                return std::unexpected{std::move(reset.error())};
            }

            RenderGraph graph;
            const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
            const auto transientColor = graph.createTransientImage(RenderGraphImageDesc{
                .name = "TransientColor",
                .format = basicRenderGraphImageFormat(frame.format),
                .extent = basicRenderGraphExtent(frame.extent),
            });

            std::vector<VulkanRenderGraphImageBinding> bindings;
            bindings.reserve(2);
            bindings.push_back(basicBackbufferBinding(backbuffer, frame));
            const BasicTransferClearParams transientClearParams{
                .color = {0.12F, 0.20F, 0.16F, 1.0F},
            };
            const BasicTransferClearParams presentClearParams{
                .color =
                    {
                        frame.clearColor.float32[0],
                        frame.clearColor.float32[1],
                        frame.clearColor.float32[2],
                        frame.clearColor.float32[3],
                    },
            };

            graph.addPass("ClearTransient", kBasicTransferClearPassType)
                .setParams(kBasicTransferClearParamsType, transientClearParams)
                .writeTransfer("target", transientColor)
                .recordCommands([transientClearParams](RenderGraphCommandList& commands) {
                    commands.clearColor("target", transientClearParams.color);
                })
                .execute([&frame, &bindings,
                          transientClearParams](RenderGraphPassContext pass) -> Result<void> {
                    [[maybe_unused]] const auto debugLabel =
                        VulkanDebugLabelScope::begin(frame, pass.name);
                    auto transitions =
                        recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                    if (!transitions) {
                        return std::unexpected{std::move(transitions.error())};
                    }

                    auto image = findVulkanRenderGraphTransferWrite(pass, "target", bindings);
                    if (!image) {
                        return std::unexpected{std::move(image.error())};
                    }

                    const VkClearColorValue transientClear{{
                        transientClearParams.color[0],
                        transientClearParams.color[1],
                        transientClearParams.color[2],
                        transientClearParams.color[3],
                    }};
                    VkImageSubresourceRange clearRange{};
                    clearRange.aspectMask = image->aspectMask;
                    clearRange.baseMipLevel = 0;
                    clearRange.levelCount = 1;
                    clearRange.baseArrayLayer = 0;
                    clearRange.layerCount = 1;
                    vkCmdClearColorImage(frame.commandBuffer, image->vulkanImage,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &transientClear, 1,
                                         &clearRange);
                    return {};
                });

            graph.addPass("PresentBackbufferAfterTransient", kBasicTransientPresentPassType)
                .setParams(kBasicTransientPresentParamsType, presentClearParams)
                .readTexture("source", transientColor, RenderGraphShaderStage::Fragment)
                .writeTransfer("target", backbuffer)
                .recordCommands([presentClearParams](RenderGraphCommandList& commands) {
                    commands.clearColor("target", presentClearParams.color);
                })
                .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                    [[maybe_unused]] const auto debugLabel =
                        VulkanDebugLabelScope::begin(frame, pass.name);
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

            const RenderGraphSchemaRegistry schemas = basicRenderGraphSchemaRegistry();
            auto compiled = graph.compile(schemas);
            if (!compiled) {
                return std::unexpected{std::move(compiled.error())};
            }

            auto prepared = prepareTransientResources(*compiled, bindings);
            if (!prepared) {
                return std::unexpected{std::move(prepared.error())};
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

    private:
        [[nodiscard]] Result<void> releaseTransientResources(const VulkanFrameRecordContext& frame) {
            for (VulkanTransientImageResource& resource : transientImages_) {
                auto released = transientImagePool_.release(frame, resource);
                if (!released) {
                    return std::unexpected{std::move(released.error())};
                }
            }

            transientImages_.clear();
            return {};
        }

        [[nodiscard]] static VkImageUsageFlags
        transientUsageFlags(const RenderGraphCompileResult& compiled,
                            RenderGraphImageHandle image) {
            VkImageUsageFlags usage{};
            for (const RenderGraphCompiledPass& pass : compiled.passes) {
                if (usesImage(pass.transferWrites, image)) {
                    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                }
                if (usesImage(pass.colorWrites, image)) {
                    usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                }
                if (usesImage(pass.shaderReads, image) ||
                    usesImage(pass.depthSampledReads, image)) {
                    usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
                }
                if (usesImage(pass.depthReads, image) || usesImage(pass.depthWrites, image)) {
                    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }
            }
            return usage;
        }

        [[nodiscard]] static bool usesImage(std::span<const RenderGraphImageHandle> images,
                                            RenderGraphImageHandle image) {
            for (const RenderGraphImageHandle used : images) {
                if (used == image) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] Result<void>
        prepareTransientResources(const RenderGraphCompileResult& compiled,
                                  std::vector<VulkanRenderGraphImageBinding>& bindings) {
            for (const RenderGraphTransientImageAllocation& allocation : compiled.transientImages) {
                const VkFormat format = vulkanFormat(allocation.format);
                const VkImageAspectFlags aspectMask =
                    basicRenderGraphImageAspect(allocation.format);
                const VkImageUsageFlags usage = transientUsageFlags(compiled, allocation.image);

                auto resource = transientImagePool_.acquire(VulkanImageDesc{
                    .device = device_,
                    .allocator = allocator_,
                    .format = format,
                    .extent =
                        VkExtent2D{
                            .width = allocation.extent.width,
                            .height = allocation.extent.height,
                        },
                    .usage = usage,
                    .aspectMask = aspectMask,
                });
                if (!resource) {
                    return std::unexpected{std::move(resource.error())};
                }

                bindings.push_back(VulkanRenderGraphImageBinding{
                    .image = allocation.image,
                    .vulkanImage = resource->image.handle(),
                    .vulkanImageView = resource->imageView.handle(),
                    .aspectMask = resource->image.aspectMask(),
                });
                transientImages_.push_back(std::move(*resource));
            }

            return {};
        }

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
    };

} // namespace vke
