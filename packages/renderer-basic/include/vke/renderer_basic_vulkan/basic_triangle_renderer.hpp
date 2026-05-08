#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "vke/core/result.hpp"
#include "vke/renderer_basic/draw_item.hpp"
#include "vke/rhi_vulkan/vulkan_buffer.hpp"
#include "vke/rhi_vulkan/vulkan_frame_loop.hpp"
#include "vke/rhi_vulkan/vulkan_image.hpp"
#include "vke/rhi_vulkan/vulkan_pipeline.hpp"

namespace vke {

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

    struct BasicPipelineCacheStats {
        std::uint64_t created{};
        std::uint64_t reused{};
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
        [[nodiscard]] BasicPipelineCacheStats pipelineCacheStats() const;
        [[nodiscard]] VulkanDescriptorAllocatorStats descriptorAllocatorStats() const;
        [[nodiscard]] VulkanBufferStats bufferStats() const;

    private:
        [[nodiscard]] Result<void> ensurePipeline(VkFormat colorFormat);
        [[nodiscard]] Result<void> updateSourceDescriptor(VkImageView sourceImageView);

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
        VulkanDescriptorAllocator descriptorAllocator_;
        VkDescriptorSet descriptorSet_{VK_NULL_HANDLE};
        VulkanBuffer uniformBuffer_;
        VulkanSampler sampler_;
        VulkanTransientImagePool transientImagePool_;
        std::vector<VulkanTransientImageResource> transientImages_;
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

} // namespace vke
