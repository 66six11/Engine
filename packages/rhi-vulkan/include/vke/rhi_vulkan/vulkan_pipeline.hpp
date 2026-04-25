#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

#include "vke/core/result.hpp"

namespace vke {

    struct VulkanShaderModuleDesc {
        VkDevice device{VK_NULL_HANDLE};
        std::span<const std::uint32_t> code;
    };

    class VulkanShaderModule {
    public:
        VulkanShaderModule() = default;
        VulkanShaderModule(const VulkanShaderModule&) = delete;
        VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
        VulkanShaderModule(VulkanShaderModule&& other) noexcept;
        VulkanShaderModule& operator=(VulkanShaderModule&& other) noexcept;
        ~VulkanShaderModule();

        [[nodiscard]] static Result<VulkanShaderModule> create(const VulkanShaderModuleDesc& desc);
        [[nodiscard]] VkShaderModule handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkShaderModule shaderModule_{VK_NULL_HANDLE};
    };

    struct VulkanPipelineLayoutDesc {
        VkDevice device{VK_NULL_HANDLE};
    };

    class VulkanPipelineLayout {
    public:
        VulkanPipelineLayout() = default;
        VulkanPipelineLayout(const VulkanPipelineLayout&) = delete;
        VulkanPipelineLayout& operator=(const VulkanPipelineLayout&) = delete;
        VulkanPipelineLayout(VulkanPipelineLayout&& other) noexcept;
        VulkanPipelineLayout& operator=(VulkanPipelineLayout&& other) noexcept;
        ~VulkanPipelineLayout();

        [[nodiscard]] static Result<VulkanPipelineLayout> create(
            const VulkanPipelineLayoutDesc& desc);
        [[nodiscard]] VkPipelineLayout handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
    };

    struct VulkanGraphicsPipelineDesc {
        VkDevice device{VK_NULL_HANDLE};
        VkPipelineLayout layout{VK_NULL_HANDLE};
        VkShaderModule vertexShader{VK_NULL_HANDLE};
        VkShaderModule fragmentShader{VK_NULL_HANDLE};
        VkFormat colorFormat{VK_FORMAT_UNDEFINED};
    };

    class VulkanGraphicsPipeline {
    public:
        VulkanGraphicsPipeline() = default;
        VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
        VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
        VulkanGraphicsPipeline(VulkanGraphicsPipeline&& other) noexcept;
        VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&& other) noexcept;
        ~VulkanGraphicsPipeline();

        [[nodiscard]] static Result<VulkanGraphicsPipeline> createDynamicRendering(
            const VulkanGraphicsPipelineDesc& desc);
        [[nodiscard]] VkPipeline handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkPipeline pipeline_{VK_NULL_HANDLE};
    };

} // namespace vke
