#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/renderer_basic/draw_item.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderer_descs.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderer_stats.hpp"
#include "asharia/rhi_vulkan/vulkan_buffer.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"
#include "asharia/rhi_vulkan/vulkan_image.hpp"
#include "asharia/rhi_vulkan/vulkan_pipeline.hpp"

namespace asharia {

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
        BasicRenderViewCamera camera_{};
        bool useRenderViewCamera_{};
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