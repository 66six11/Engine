#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asharia/core/error.hpp"
#include "asharia/renderer_basic/clear_frame_graph.hpp"
#include "asharia/rendergraph/render_graph.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan_rendergraph/vulkan_render_graph.hpp"

namespace asharia {

    struct VulkanRenderGraphImageBinding {
        RenderGraphImageHandle image{};
        VkImage vulkanImage{VK_NULL_HANDLE};
        VkImageView vulkanImageView{VK_NULL_HANDLE};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
        std::string debugName;
    };

    template <typename VulkanHandle>
    [[nodiscard]] inline std::uint64_t vulkanDebugObjectHandle(VulkanHandle handle) {
        if constexpr (std::is_pointer_v<VulkanHandle>) {
            // Vulkan debug utils represents object handles as uint64_t values by specification.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
        } else {
            return static_cast<std::uint64_t>(handle);
        }
    }

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
            .vulkanImageView = frame.imageView,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .debugName = "Backbuffer[" + std::to_string(frame.imageIndex) + "]",
        };
    }

    [[nodiscard]] inline VkImageAspectFlags
    basicRenderGraphImageAspect(RenderGraphImageFormat format) {
        switch (format) {
        case RenderGraphImageFormat::D32Sfloat:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case RenderGraphImageFormat::B8G8R8A8Srgb:
        case RenderGraphImageFormat::Undefined:
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    [[nodiscard]] inline Result<VulkanRenderGraphImageBinding>
    findVulkanRenderGraphImage(RenderGraphImageHandle image,
                               std::span<const VulkanRenderGraphImageBinding> bindings) {
        for (const VulkanRenderGraphImageBinding& binding : bindings) {
            if (binding.image == image && binding.vulkanImage != VK_NULL_HANDLE) {
                return binding;
            }
        }

        return std::unexpected{Error{
            ErrorDomain::RenderGraph,
            0,
            "RenderGraph image is not bound to a Vulkan image.",
        }};
    }

    [[nodiscard]] inline Result<RenderGraphImageHandle>
    findRenderGraphImageSlot(std::span<const RenderGraphImageSlot> slots, std::string_view slotName,
                             std::string_view passName) {
        for (const RenderGraphImageSlot& slot : slots) {
            if (std::string_view{slot.name} == slotName) {
                return slot.image;
            }
        }

        return std::unexpected{Error{
            ErrorDomain::RenderGraph,
            0,
            "RenderGraph pass '" + std::string{passName} + "' does not declare image slot '" +
                std::string{slotName} + "'.",
        }};
    }

    [[nodiscard]] inline Result<VulkanRenderGraphImageBinding>
    findVulkanRenderGraphImageSlot(std::span<const RenderGraphImageSlot> slots,
                                   std::string_view slotName, RenderGraphPassContext pass,
                                   std::span<const VulkanRenderGraphImageBinding> bindings) {
        auto image = findRenderGraphImageSlot(slots, slotName, pass.name);
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }

        return findVulkanRenderGraphImage(*image, bindings);
    }

    [[nodiscard]] inline Result<VulkanRenderGraphImageBinding>
    findVulkanRenderGraphShaderRead(RenderGraphPassContext pass, std::string_view slotName,
                                    std::span<const VulkanRenderGraphImageBinding> bindings) {
        return findVulkanRenderGraphImageSlot(pass.shaderReadSlots, slotName, pass, bindings);
    }

    [[nodiscard]] inline Result<VulkanRenderGraphImageBinding>
    findVulkanRenderGraphColorWrite(RenderGraphPassContext pass, std::string_view slotName,
                                    std::span<const VulkanRenderGraphImageBinding> bindings) {
        return findVulkanRenderGraphImageSlot(pass.colorWriteSlots, slotName, pass, bindings);
    }

    [[nodiscard]] inline Result<VulkanRenderGraphImageBinding>
    findVulkanRenderGraphDepthWrite(RenderGraphPassContext pass, std::string_view slotName,
                                    std::span<const VulkanRenderGraphImageBinding> bindings) {
        return findVulkanRenderGraphImageSlot(pass.depthWriteSlots, slotName, pass, bindings);
    }

    [[nodiscard]] inline Result<VulkanRenderGraphImageBinding>
    findVulkanRenderGraphTransferWrite(RenderGraphPassContext pass, std::string_view slotName,
                                       std::span<const VulkanRenderGraphImageBinding> bindings) {
        return findVulkanRenderGraphImageSlot(pass.transferWriteSlots, slotName, pass, bindings);
    }

    [[nodiscard]] inline std::string
    renderGraphDebugImageName(RenderGraphImageHandle image,
                              std::span<const VulkanRenderGraphImageBinding> bindings) {
        for (const VulkanRenderGraphImageBinding& binding : bindings) {
            if (binding.image == image && !binding.debugName.empty()) {
                return binding.debugName;
            }
        }

        return "Image#" + std::to_string(image.index);
    }

    [[nodiscard]] inline std::string renderGraphImageStateLabel(RenderGraphImageState state,
                                                                RenderGraphShaderStage shaderStage) {
        std::string label;
        switch (state) {
        case RenderGraphImageState::Undefined:
            label = "Undefined";
            break;
        case RenderGraphImageState::ColorAttachment:
            label = "ColorAttachment";
            break;
        case RenderGraphImageState::ShaderRead:
            label = "ShaderRead";
            break;
        case RenderGraphImageState::DepthAttachmentRead:
            label = "DepthAttachmentRead";
            break;
        case RenderGraphImageState::DepthAttachmentWrite:
            label = "DepthAttachmentWrite";
            break;
        case RenderGraphImageState::DepthSampledRead:
            label = "DepthSampledRead";
            break;
        case RenderGraphImageState::TransferDst:
            label = "TransferDst";
            break;
        case RenderGraphImageState::Present:
            label = "Present";
            break;
        }

        switch (shaderStage) {
        case RenderGraphShaderStage::Fragment:
            label += "(fragment)";
            break;
        case RenderGraphShaderStage::Compute:
            label += "(compute)";
            break;
        case RenderGraphShaderStage::None:
            break;
        }
        return label;
    }

    inline void appendRenderGraphImageSlotLabels(
        std::string& label, std::string_view group,
        std::span<const RenderGraphImageSlot> slots,
        std::span<const VulkanRenderGraphImageBinding> bindings) {
        for (const RenderGraphImageSlot& slot : slots) {
            label += " ";
            label += group;
            label += ".";
            label += slot.name;
            label += "=";
            label += renderGraphDebugImageName(slot.image, bindings);
        }
    }

    [[nodiscard]] inline std::string
    renderGraphPassDebugLabel(RenderGraphPassContext pass,
                              std::span<const VulkanRenderGraphImageBinding> bindings) {
        std::string label{"RG Pass: "};
        label += pass.name;
        if (!pass.type.empty()) {
            label += " [";
            label += pass.type;
            label += "]";
        }

        appendRenderGraphImageSlotLabels(label, "color", pass.colorWriteSlots, bindings);
        appendRenderGraphImageSlotLabels(label, "read", pass.shaderReadSlots, bindings);
        appendRenderGraphImageSlotLabels(label, "depthRead", pass.depthReadSlots, bindings);
        appendRenderGraphImageSlotLabels(label, "depthWrite", pass.depthWriteSlots, bindings);
        appendRenderGraphImageSlotLabels(label, "depthSample", pass.depthSampledReadSlots, bindings);
        appendRenderGraphImageSlotLabels(label, "transfer", pass.transferWriteSlots, bindings);
        return label;
    }

    [[nodiscard]] inline std::string
    renderGraphTransitionDebugLabel(const RenderGraphImageTransition& transition,
                                    std::span<const VulkanRenderGraphImageBinding> bindings) {
        std::string label{"RG Barrier: "};
        label += renderGraphDebugImageName(transition.image, bindings);
        label += " ";
        label += renderGraphImageStateLabel(transition.oldState, transition.oldShaderStage);
        label += " -> ";
        label += renderGraphImageStateLabel(transition.newState, transition.newShaderStage);
        return label;
    }

    [[nodiscard]] inline Result<void>
    setVulkanRenderGraphImageDebugNames(const VulkanFrameRecordContext& frame,
                                        const VulkanRenderGraphImageBinding& binding) {
        if (binding.debugName.empty()) {
            return {};
        }

        auto namedImage = frame.setDebugObjectName(
            VK_OBJECT_TYPE_IMAGE, vulkanDebugObjectHandle(binding.vulkanImage),
            "RG.Image." + binding.debugName);
        if (!namedImage) {
            return std::unexpected{std::move(namedImage.error())};
        }

        auto namedImageView = frame.setDebugObjectName(
            VK_OBJECT_TYPE_IMAGE_VIEW, vulkanDebugObjectHandle(binding.vulkanImageView),
            "RG.ImageView." + binding.debugName);
        if (!namedImageView) {
            return std::unexpected{std::move(namedImageView.error())};
        }

        return {};
    }

    [[nodiscard]] inline Result<void>
    recordRenderGraphImageBarrier(VkCommandBuffer commandBuffer,
                                  const RenderGraphImageTransition& transition,
                                  std::span<const VulkanRenderGraphImageBinding> bindings) {
        auto image = findVulkanRenderGraphImage(transition.image, bindings);
        if (!image) {
            return std::unexpected{std::move(image.error())};
        }

        const VkImageMemoryBarrier2 barrier =
            vulkanImageBarrier(transition, image->vulkanImage, image->aspectMask);
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
            const std::string debugLabel = renderGraphTransitionDebugLabel(transition, bindings);
            [[maybe_unused]] const auto debugScope =
                VulkanDebugLabelScope::begin(frame, debugLabel);
            auto recorded =
                recordRenderGraphImageBarrier(frame.commandBuffer, transition, bindings);
            if (!recorded) {
                return std::unexpected{std::move(recorded.error())};
            }
        }

        return {};
    }

} // namespace asharia
