#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderer_descs.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderer_stats.hpp"
#include "asharia/renderer_basic_vulkan/render_view.hpp"
#include "asharia/rhi_vulkan/vulkan_buffer.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan/vulkan_image.hpp"
#include "asharia/rhi_vulkan/vulkan_pipeline.hpp"

namespace asharia {

    class BasicFullscreenTextureRenderer {
    public:
        BasicFullscreenTextureRenderer() = default;
        BasicFullscreenTextureRenderer(const BasicFullscreenTextureRenderer&) = delete;
        BasicFullscreenTextureRenderer& operator=(const BasicFullscreenTextureRenderer&) = delete;
        BasicFullscreenTextureRenderer(BasicFullscreenTextureRenderer&& other) noexcept;
        BasicFullscreenTextureRenderer& operator=(BasicFullscreenTextureRenderer&& other) noexcept;
        ~BasicFullscreenTextureRenderer() = default;

        [[nodiscard]] static Result<BasicFullscreenTextureRenderer>
        create(const BasicFullscreenTextureRendererDesc& desc);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordFrame(const VulkanFrameRecordContext& frame);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordViewFrame(const VulkanFrameRecordContext& frame, BasicRenderViewDesc view);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordOffscreenViewportFrame(const VulkanFrameRecordContext& frame);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordOffscreenViewportFrame(const VulkanFrameRecordContext& frame,
                                     VkExtent2D viewportExtent);
        [[nodiscard]] BasicPipelineCacheStats pipelineCacheStats() const;
        [[nodiscard]] BasicOffscreenViewportStats offscreenViewportStats() const;
        [[nodiscard]] BasicOffscreenViewportTarget offscreenViewportTarget() const;
        [[nodiscard]] VulkanDescriptorAllocatorStats descriptorAllocatorStats() const;
        [[nodiscard]] VulkanBufferStats bufferStats() const;

    private:
        [[nodiscard]] Result<void> ensurePipeline(VkFormat colorFormat);
        [[nodiscard]] Result<void>
        ensureDebugLinePipeline(VkFormat colorFormat, BasicRenderViewOverlayBlendMode blendMode);
        [[nodiscard]] VkDescriptorSet
        acquireFullscreenDescriptorSet(const VulkanFrameRecordContext& frame);
        [[nodiscard]] VkDescriptorSet
        acquireCompositeDescriptorSet(const VulkanFrameRecordContext& frame);
        [[nodiscard]] Result<VkBuffer>
        uploadDebugLineVertices(const VulkanFrameRecordContext& frame,
                                std::span<const std::byte> vertices);
        [[nodiscard]] Result<void>
        ensureOffscreenViewportTarget(const VulkanFrameRecordContext& frame, VkFormat format,
                                      VkExtent2D extent);
        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanShaderModule vertexShader_;
        VulkanShaderModule fragmentShader_;
        VulkanShaderModule debugLineVertexShader_;
        VulkanShaderModule debugLineFragmentShader_;
        std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts_;
        VulkanPipelineLayout pipelineLayout_;
        VulkanPipelineLayout debugLinePipelineLayout_;
        VulkanPipelineCache pipelineCache_;
        VulkanGraphicsPipeline pipeline_;
        VulkanGraphicsPipeline debugLinePipeline_;
        VkFormat pipelineFormat_{VK_FORMAT_UNDEFINED};
        VkFormat debugLinePipelineFormat_{VK_FORMAT_UNDEFINED};
        BasicRenderViewOverlayBlendMode debugLinePipelineBlendMode_{
            BasicRenderViewOverlayBlendMode::AlphaBlend};
        BasicPipelineCacheStats pipelineCacheStats_;
        VulkanRenderTarget offscreenViewportTarget_;
        VulkanDescriptorAllocator descriptorAllocator_;
        std::vector<VkDescriptorSet> descriptorSets_;
        std::vector<VkDescriptorSet> compositeDescriptorSets_;
        std::uint64_t descriptorSetEpoch_{};
        std::uint64_t compositeDescriptorSetEpoch_{};
        std::size_t descriptorSetCursor_{};
        std::size_t compositeDescriptorSetCursor_{};
        std::vector<VulkanBuffer> debugLineVertexBuffers_;
        std::vector<VkDeviceSize> debugLineVertexBufferSizes_;
        std::uint64_t debugLineVertexBufferEpoch_{};
        std::size_t debugLineVertexBufferCursor_{};
        VulkanBuffer uniformBuffer_;
        VulkanSampler sampler_;
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
    };

} // namespace asharia
