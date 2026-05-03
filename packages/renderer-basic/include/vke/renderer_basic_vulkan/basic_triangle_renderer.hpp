#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
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
        BasicDrawItem drawItem{basicTriangleDrawItem()};
    };

    struct BasicDescriptorLayoutSmokeDesc {
        VkDevice device{VK_NULL_HANDLE};
        std::filesystem::path shaderDirectory;
    };

    [[nodiscard]] Result<void>
    validateBasicDescriptorLayoutSmoke(const BasicDescriptorLayoutSmokeDesc& desc);

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

    private:
        [[nodiscard]] Result<void> ensurePipeline(VkFormat colorFormat,
                                                  VkFormat depthFormat = VK_FORMAT_UNDEFINED);

        VkDevice device_{VK_NULL_HANDLE};
        VmaAllocator allocator_{};
        VulkanShaderModule vertexShader_;
        VulkanShaderModule fragmentShader_;
        std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts_;
        VulkanPipelineLayout pipelineLayout_;
        VulkanGraphicsPipeline pipeline_;
        VulkanBuffer vertexBuffer_;
        VkFormat pipelineFormat_{VK_FORMAT_UNDEFINED};
        VkFormat pipelineDepthFormat_{VK_FORMAT_UNDEFINED};
        std::vector<VulkanImage> transientImages_;
        std::vector<VulkanImageView> transientImageViews_;
        BasicDrawItem drawItem_{basicTriangleDrawItem()};
    };

} // namespace vke
