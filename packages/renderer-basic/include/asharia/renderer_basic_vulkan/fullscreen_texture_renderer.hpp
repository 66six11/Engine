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

    class BasicFullscreenTextureRenderer;

    class BasicRenderFrameResourceContext final {
    public:
        BasicRenderFrameResourceContext(const BasicRenderFrameResourceContext&) = delete;
        BasicRenderFrameResourceContext& operator=(const BasicRenderFrameResourceContext&) = delete;
        BasicRenderFrameResourceContext(BasicRenderFrameResourceContext&&) noexcept = default;
        BasicRenderFrameResourceContext&
        operator=(BasicRenderFrameResourceContext&&) noexcept = default;
        ~BasicRenderFrameResourceContext() = default;

        [[nodiscard]] std::size_t index() const noexcept;

    private:
        friend class BasicFullscreenTextureRenderer;

        explicit BasicRenderFrameResourceContext(std::size_t index) noexcept;
        void beginFrame() noexcept;

        std::size_t index_{};
        std::size_t fullscreenDescriptorCursor_{};
        std::size_t compositeDescriptorCursor_{};
        std::size_t selectionOutlineDescriptorCursor_{};
        std::size_t debugLineVertexBufferCursor_{};
    };

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
        recordViewFrame(const VulkanFrameRecordContext& frame, BasicRenderViewDesc view,
                        VulkanTransientImagePool& transientImagePool,
                        std::vector<VulkanTransientImageResource>& transientImages);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordViewFrame(const VulkanFrameRecordContext& frame, BasicRenderViewDesc view,
                        BasicRenderFrameResourceContext& frameResources,
                        VulkanTransientImagePool& transientImagePool,
                        std::vector<VulkanTransientImageResource>& transientImages);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordOffscreenViewportFrame(const VulkanFrameRecordContext& frame);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordOffscreenViewportFrame(const VulkanFrameRecordContext& frame,
                                     VkExtent2D viewportExtent);
        [[nodiscard]] Result<BasicRenderFrameResourceContext>
        createFrameResourceContext(std::size_t index) const;
        void resetFrameResourceCursors() noexcept;
        [[nodiscard]] BasicPipelineCacheStats pipelineCacheStats() const;
        [[nodiscard]] BasicPipelineCacheStats worldGridPipelineCacheStats() const;
        [[nodiscard]] BasicPipelineCacheStats debugLinePipelineCacheStats() const;
        [[nodiscard]] BasicPipelineCacheStats sceneMeshPipelineCacheStats() const;
        [[nodiscard]] BasicOffscreenViewportStats offscreenViewportStats() const;
        [[nodiscard]] BasicOffscreenViewportTarget offscreenViewportTarget() const;
        [[nodiscard]] VulkanDescriptorAllocatorStats descriptorAllocatorStats() const;
        [[nodiscard]] VulkanBufferStats bufferStats() const;

    private:
        struct FullscreenPipelineCacheEntry {
            VkFormat colorFormat{VK_FORMAT_UNDEFINED};
            VulkanGraphicsPipeline pipeline;
        };

        struct SceneMeshPipelineCacheEntry {
            VkFormat colorFormat{VK_FORMAT_UNDEFINED};
            VkFormat depthFormat{VK_FORMAT_UNDEFINED};
            BasicSceneRasterMode rasterMode{BasicSceneRasterMode::Solid};
            VulkanGraphicsPipeline pipeline;
        };

        struct OverlayPipelineCacheEntry {
            VkFormat colorFormat{VK_FORMAT_UNDEFINED};
            BasicRenderViewOverlayBlendMode blendMode{BasicRenderViewOverlayBlendMode::AlphaBlend};
            VulkanGraphicsPipeline pipeline;
        };

        [[nodiscard]] Result<VkPipeline> ensurePipeline(VkFormat colorFormat);
        [[nodiscard]] Result<VkPipeline>
        ensureWorldGridPipeline(VkFormat colorFormat, BasicRenderViewOverlayBlendMode blendMode);
        [[nodiscard]] Result<VkPipeline>
        ensureDebugLinePipeline(VkFormat colorFormat, BasicRenderViewOverlayBlendMode blendMode);
        [[nodiscard]] Result<VkPipeline> ensureSceneMeshPipeline(VkFormat colorFormat,
                                                                 VkFormat depthFormat,
                                                                 BasicSceneRasterMode rasterMode);
        [[nodiscard]] Result<VkPipeline> ensureSelectionMaskPipeline(VkFormat depthFormat);
        [[nodiscard]] Result<VkPipeline> ensureSelectionOutlinePipeline(VkFormat colorFormat);
        [[nodiscard]] Result<void> ensureSceneMeshResources();
        [[nodiscard]] VkDescriptorSet
        acquireFullscreenDescriptorSet(const VulkanFrameRecordContext& frame,
                                       BasicRenderFrameResourceContext* frameResources);
        [[nodiscard]] VkDescriptorSet
        acquireCompositeDescriptorSet(const VulkanFrameRecordContext& frame,
                                      BasicRenderFrameResourceContext* frameResources);
        [[nodiscard]] VkDescriptorSet
        acquireSelectionOutlineDescriptorSet(const VulkanFrameRecordContext& frame,
                                             BasicRenderFrameResourceContext* frameResources);
        [[nodiscard]] Result<VkBuffer>
        uploadDebugLineVertices(const VulkanFrameRecordContext& frame,
                                std::span<const std::byte> vertices,
                                BasicRenderFrameResourceContext* frameResources);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordViewFrame(const VulkanFrameRecordContext& frame, BasicRenderViewDesc view,
                        BasicRenderFrameResourceContext* frameResources,
                        VulkanTransientImagePool& transientImagePool,
                        std::vector<VulkanTransientImageResource>& transientImages);
        [[nodiscard]] Result<void>
        ensureOffscreenViewportTarget(const VulkanFrameRecordContext& frame, VkFormat format,
                                      VkExtent2D extent);
        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanShaderModule vertexShader_;
        VulkanShaderModule fragmentShader_;
        VulkanShaderModule worldGridVertexShader_;
        VulkanShaderModule worldGridFragmentShader_;
        VulkanShaderModule debugLineVertexShader_;
        VulkanShaderModule debugLineFragmentShader_;
        VulkanShaderModule sceneMeshVertexShader_;
        VulkanShaderModule sceneMeshFragmentShader_;
        VulkanShaderModule selectionMaskFragmentShader_;
        VulkanShaderModule selectionOutlineVertexShader_;
        VulkanShaderModule selectionOutlineFragmentShader_;
        std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts_;
        std::vector<VulkanDescriptorSetLayout> selectionOutlineDescriptorSetLayouts_;
        VulkanPipelineLayout pipelineLayout_;
        VulkanPipelineLayout worldGridPipelineLayout_;
        VulkanPipelineLayout debugLinePipelineLayout_;
        VulkanPipelineLayout sceneMeshPipelineLayout_;
        VulkanPipelineLayout selectionOutlinePipelineLayout_;
        VulkanPipelineCache pipelineCache_;
        std::vector<FullscreenPipelineCacheEntry> fullscreenPipelines_;
        std::vector<OverlayPipelineCacheEntry> worldGridPipelines_;
        std::vector<OverlayPipelineCacheEntry> debugLinePipelines_;
        std::vector<SceneMeshPipelineCacheEntry> sceneMeshPipelines_;
        VulkanGraphicsPipeline selectionMaskPipeline_;
        std::vector<FullscreenPipelineCacheEntry> selectionOutlinePipelines_;
        BasicPipelineCacheStats pipelineCacheStats_;
        BasicPipelineCacheStats worldGridPipelineCacheStats_;
        BasicPipelineCacheStats debugLinePipelineCacheStats_;
        BasicPipelineCacheStats sceneMeshPipelineCacheStats_;
        VulkanRenderTarget offscreenViewportTarget_;
        VulkanDescriptorAllocator descriptorAllocator_;
        std::vector<VkDescriptorSet> descriptorSets_;
        std::vector<VkDescriptorSet> compositeDescriptorSets_;
        std::vector<VkDescriptorSet> selectionOutlineDescriptorSets_;
        std::uint64_t descriptorSetEpoch_{};
        std::uint64_t compositeDescriptorSetEpoch_{};
        std::uint64_t selectionOutlineDescriptorSetEpoch_{};
        std::size_t descriptorSetCursor_{};
        std::size_t compositeDescriptorSetCursor_{};
        std::size_t selectionOutlineDescriptorSetCursor_{};
        std::vector<VulkanBuffer> debugLineVertexBuffers_;
        std::vector<VkDeviceSize> debugLineVertexBufferSizes_;
        std::uint64_t debugLineVertexBufferEpoch_{};
        std::size_t debugLineVertexBufferCursor_{};
        VulkanBuffer sceneMeshVertexBuffer_;
        VulkanBuffer sceneMeshIndexBuffer_;
        VulkanBuffer uniformBuffer_;
        VulkanSampler sampler_;
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
        VulkanDeviceCapabilities deviceCapabilities_;
    };

} // namespace asharia
