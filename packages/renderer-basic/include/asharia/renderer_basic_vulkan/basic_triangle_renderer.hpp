#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/renderer_basic/draw_item.hpp"
#include "asharia/rhi_vulkan/vulkan_buffer.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan/vulkan_image.hpp"
#include "asharia/rhi_vulkan/vulkan_pipeline.hpp"

namespace asharia {

    struct BasicTriangleRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
        BasicMeshKind meshKind{BasicMeshKind::Triangle};
        BasicDrawItem drawItem{basicTriangleDrawItem()};
    };

    struct BasicDescriptorLayoutSmokeDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
    };

    struct BasicFullscreenTextureRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
    };

    struct BasicMrtRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
    };

    struct BasicMesh3DRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
    };

    struct BasicDrawListRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
        std::span<const BasicDrawListItem> drawItems{};
    };

    struct BasicComputeDispatchRendererDesc {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        std::filesystem::path shaderDirectory;
        bool graphicsQueueSupportsCompute{};
    };

    struct BasicPipelineCacheStats {
        std::uint64_t created{};
        std::uint64_t reused{};
    };

    struct BasicOffscreenViewportStats {
        std::uint64_t renderTargetsCreated{};
        std::uint64_t renderTargetsReused{};
        std::uint64_t renderTargetsDeferredForDeletion{};
    };

    struct BasicComputeDispatchStats {
        std::uint64_t dispatchesRecorded{};
    };

    struct BasicOffscreenViewportTarget {
        VkImage image{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageLayout sampledLayout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    };

    enum class BasicRenderViewTargetFinalUsage {
        Present,
        SampledTexture,
    };

    struct BasicRenderViewTarget {
        VkImage image{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
        BasicRenderViewTargetFinalUsage finalUsage{BasicRenderViewTargetFinalUsage::Present};
    };

    struct BasicRenderViewDesc {
        BasicRenderViewTarget target{};
    };

    [[nodiscard]] Result<void>
    validateBasicDescriptorLayoutSmoke(const BasicDescriptorLayoutSmokeDesc& desc);

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
        [[nodiscard]] Result<void> ensureOffscreenViewportTarget(const VulkanFrameRecordContext& frame,
                                                                 VkFormat format,
                                                                 VkExtent2D extent);
        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanShaderModule vertexShader_;
        VulkanShaderModule fragmentShader_;
        std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts_;
        VulkanPipelineLayout pipelineLayout_;
        VulkanPipelineCache pipelineCache_;
        VulkanGraphicsPipeline pipeline_;
        VkFormat pipelineFormat_{VK_FORMAT_UNDEFINED};
        BasicPipelineCacheStats pipelineCacheStats_;
        VulkanRenderTarget offscreenViewportTarget_;
        VulkanDescriptorAllocator descriptorAllocator_;
        VkDescriptorSet descriptorSet_{VK_NULL_HANDLE};
        VkDescriptorSet compositeDescriptorSet_{VK_NULL_HANDLE};
        VulkanBuffer uniformBuffer_;
        VulkanSampler sampler_;
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
    };

    class BasicMrtRenderer {
    public:
        BasicMrtRenderer() = default;
        BasicMrtRenderer(const BasicMrtRenderer&) = delete;
        BasicMrtRenderer& operator=(const BasicMrtRenderer&) = delete;
        BasicMrtRenderer(BasicMrtRenderer&& other) noexcept;
        BasicMrtRenderer& operator=(BasicMrtRenderer&& other) noexcept;
        ~BasicMrtRenderer() = default;

        [[nodiscard]] static Result<BasicMrtRenderer> create(const BasicMrtRendererDesc& desc);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordFrame(const VulkanFrameRecordContext& frame);
        [[nodiscard]] VulkanTransientImagePoolStats transientPoolStats() const;

    private:
        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
    };

    class BasicComputeDispatchRenderer {
    public:
        BasicComputeDispatchRenderer() = default;
        BasicComputeDispatchRenderer(const BasicComputeDispatchRenderer&) = delete;
        BasicComputeDispatchRenderer& operator=(const BasicComputeDispatchRenderer&) = delete;
        BasicComputeDispatchRenderer(BasicComputeDispatchRenderer&& other) noexcept;
        BasicComputeDispatchRenderer& operator=(BasicComputeDispatchRenderer&& other) noexcept;
        ~BasicComputeDispatchRenderer() = default;

        [[nodiscard]] static Result<BasicComputeDispatchRenderer>
        create(const BasicComputeDispatchRendererDesc& desc);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordFrame(const VulkanFrameRecordContext& frame);
        [[nodiscard]] Result<std::array<std::uint32_t, 4>> readbackValuesAfterGpuComplete();
        [[nodiscard]] BasicPipelineCacheStats pipelineCacheStats() const;
        [[nodiscard]] VulkanDescriptorAllocatorStats descriptorAllocatorStats() const;
        [[nodiscard]] VulkanBufferStats bufferStats() const;
        [[nodiscard]] BasicComputeDispatchStats computeStats() const;

    private:
        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanShaderModule computeShader_;
        std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts_;
        VulkanPipelineLayout pipelineLayout_;
        VulkanPipelineCache pipelineCache_;
        VulkanComputePipeline pipeline_;
        BasicPipelineCacheStats pipelineCacheStats_;
        VulkanDescriptorAllocator descriptorAllocator_;
        VkDescriptorSet descriptorSet_{VK_NULL_HANDLE};
        VulkanBuffer storageBuffer_;
        VulkanBuffer readbackBuffer_;
        BasicComputeDispatchStats computeStats_;
    };

    class BasicTriangleRenderer {
    public:
        BasicTriangleRenderer() = default;
        BasicTriangleRenderer(const BasicTriangleRenderer&) = delete;
        BasicTriangleRenderer& operator=(const BasicTriangleRenderer&) = delete;
        BasicTriangleRenderer(BasicTriangleRenderer&& other) noexcept;
        BasicTriangleRenderer& operator=(BasicTriangleRenderer&& other) noexcept;
        ~BasicTriangleRenderer() = default;

        [[nodiscard]] static Result<BasicTriangleRenderer>
        create(const BasicTriangleRendererDesc& desc);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordFrame(const VulkanFrameRecordContext& frame);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordFrameWithDepth(const VulkanFrameRecordContext& frame);
        [[nodiscard]] BasicPipelineCacheStats pipelineCacheStats() const;
        [[nodiscard]] VulkanBufferStats bufferStats() const;

    private:
        [[nodiscard]] Result<void> ensurePipeline(VkFormat colorFormat,
                                                  VkFormat depthFormat = VK_FORMAT_UNDEFINED);

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanShaderModule vertexShader_;
        VulkanShaderModule fragmentShader_;
        std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts_;
        VulkanPipelineLayout pipelineLayout_;
        VulkanPipelineCache pipelineCache_;
        VulkanGraphicsPipeline pipeline_;
        VulkanBuffer vertexBuffer_;
        VulkanBuffer indexBuffer_;
        VkFormat pipelineFormat_{VK_FORMAT_UNDEFINED};
        VkFormat pipelineDepthFormat_{VK_FORMAT_UNDEFINED};
        BasicPipelineCacheStats pipelineCacheStats_;
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
        BasicDrawItem drawItem_{basicTriangleDrawItem()};
    };

    class BasicMesh3DRenderer {
    public:
        BasicMesh3DRenderer() = default;
        BasicMesh3DRenderer(const BasicMesh3DRenderer&) = delete;
        BasicMesh3DRenderer& operator=(const BasicMesh3DRenderer&) = delete;
        BasicMesh3DRenderer(BasicMesh3DRenderer&& other) noexcept;
        BasicMesh3DRenderer& operator=(BasicMesh3DRenderer&& other) noexcept;
        ~BasicMesh3DRenderer() = default;

        [[nodiscard]] static Result<BasicMesh3DRenderer>
        create(const BasicMesh3DRendererDesc& desc);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordFrame(const VulkanFrameRecordContext& frame);
        [[nodiscard]] BasicPipelineCacheStats pipelineCacheStats() const;
        [[nodiscard]] VulkanBufferStats bufferStats() const;

    private:
        [[nodiscard]] Result<void> ensurePipeline(VkFormat colorFormat, VkFormat depthFormat);

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanShaderModule vertexShader_;
        VulkanShaderModule fragmentShader_;
        std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts_;
        VulkanPipelineLayout pipelineLayout_;
        VulkanPipelineCache pipelineCache_;
        VulkanGraphicsPipeline pipeline_;
        VulkanBuffer vertexBuffer_;
        VulkanBuffer indexBuffer_;
        VkFormat pipelineFormat_{VK_FORMAT_UNDEFINED};
        VkFormat pipelineDepthFormat_{VK_FORMAT_UNDEFINED};
        BasicPipelineCacheStats pipelineCacheStats_;
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
    };

    class BasicDrawListRenderer {
    public:
        BasicDrawListRenderer() = default;
        BasicDrawListRenderer(const BasicDrawListRenderer&) = delete;
        BasicDrawListRenderer& operator=(const BasicDrawListRenderer&) = delete;
        BasicDrawListRenderer(BasicDrawListRenderer&& other) noexcept;
        BasicDrawListRenderer& operator=(BasicDrawListRenderer&& other) noexcept;
        ~BasicDrawListRenderer() = default;

        [[nodiscard]] static Result<BasicDrawListRenderer>
        create(const BasicDrawListRendererDesc& desc);
        [[nodiscard]] Result<VulkanFrameRecordResult>
        recordFrame(const VulkanFrameRecordContext& frame);
        [[nodiscard]] BasicPipelineCacheStats pipelineCacheStats() const;
        [[nodiscard]] VulkanBufferStats bufferStats() const;

    private:
        [[nodiscard]] Result<void> ensurePipeline(VkFormat colorFormat, VkFormat depthFormat);

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanShaderModule vertexShader_;
        VulkanShaderModule fragmentShader_;
        VulkanPipelineLayout pipelineLayout_;
        VulkanPipelineCache pipelineCache_;
        VulkanGraphicsPipeline pipeline_;
        VulkanBuffer vertexBuffer_;
        VulkanBuffer indexBuffer_;
        VkFormat pipelineFormat_{VK_FORMAT_UNDEFINED};
        VkFormat pipelineDepthFormat_{VK_FORMAT_UNDEFINED};
        BasicPipelineCacheStats pipelineCacheStats_;
        std::vector<BasicDrawListItem> drawItems_;
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
    };

} // namespace asharia
