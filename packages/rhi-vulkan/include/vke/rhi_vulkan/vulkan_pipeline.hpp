#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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

    struct VulkanDescriptorSetLayoutDesc {
        VkDevice device{VK_NULL_HANDLE};
        std::span<const VkDescriptorSetLayoutBinding> bindings;
        VkDescriptorSetLayoutCreateFlags flags{};
    };

    class VulkanDescriptorSetLayout {
    public:
        VulkanDescriptorSetLayout() = default;
        VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
        VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;
        VulkanDescriptorSetLayout(VulkanDescriptorSetLayout&& other) noexcept;
        VulkanDescriptorSetLayout& operator=(VulkanDescriptorSetLayout&& other) noexcept;
        ~VulkanDescriptorSetLayout();

        [[nodiscard]] static Result<VulkanDescriptorSetLayout>
        create(const VulkanDescriptorSetLayoutDesc& desc);
        [[nodiscard]] VkDescriptorSetLayout handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkDescriptorSetLayout descriptorSetLayout_{VK_NULL_HANDLE};
    };

    struct VulkanDescriptorPoolSize {
        VkDescriptorType type{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
        std::uint32_t count{};
    };

    struct VulkanDescriptorPoolDesc {
        VkDevice device{VK_NULL_HANDLE};
        std::uint32_t maxSets{};
        std::span<const VulkanDescriptorPoolSize> poolSizes;
        VkDescriptorPoolCreateFlags flags{};
    };

    struct VulkanDescriptorSetAllocationDesc {
        std::span<const VkDescriptorSetLayout> setLayouts;
    };

    struct VulkanDescriptorBufferWrite {
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        std::uint32_t binding{};
        std::uint32_t arrayElement{};
        VkDescriptorType descriptorType{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceSize offset{};
        VkDeviceSize range{VK_WHOLE_SIZE};
    };

    struct VulkanDescriptorImageWrite {
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        std::uint32_t binding{};
        std::uint32_t arrayElement{};
        VkDescriptorType descriptorType{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
        VkImageLayout imageLayout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    };

    class VulkanDescriptorPool {
    public:
        VulkanDescriptorPool() = default;
        VulkanDescriptorPool(const VulkanDescriptorPool&) = delete;
        VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;
        VulkanDescriptorPool(VulkanDescriptorPool&& other) noexcept;
        VulkanDescriptorPool& operator=(VulkanDescriptorPool&& other) noexcept;
        ~VulkanDescriptorPool();

        [[nodiscard]] static Result<VulkanDescriptorPool>
        create(const VulkanDescriptorPoolDesc& desc);
        [[nodiscard]] Result<std::vector<VkDescriptorSet>>
        allocate(const VulkanDescriptorSetAllocationDesc& desc) const;
        [[nodiscard]] VkDescriptorPool handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
    };

    void updateVulkanDescriptorBuffers(VkDevice device,
                                       std::span<const VulkanDescriptorBufferWrite> writes);
    void updateVulkanDescriptorImages(VkDevice device,
                                      std::span<const VulkanDescriptorImageWrite> writes);

    struct VulkanPipelineLayoutDesc {
        VkDevice device{VK_NULL_HANDLE};
        std::span<const VkDescriptorSetLayout> setLayouts;
        std::span<const VkPushConstantRange> pushConstantRanges;
    };

    class VulkanPipelineLayout {
    public:
        VulkanPipelineLayout() = default;
        VulkanPipelineLayout(const VulkanPipelineLayout&) = delete;
        VulkanPipelineLayout& operator=(const VulkanPipelineLayout&) = delete;
        VulkanPipelineLayout(VulkanPipelineLayout&& other) noexcept;
        VulkanPipelineLayout& operator=(VulkanPipelineLayout&& other) noexcept;
        ~VulkanPipelineLayout();

        [[nodiscard]] static Result<VulkanPipelineLayout>
        create(const VulkanPipelineLayoutDesc& desc);
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
        std::string_view vertexEntryPoint{"main"};
        std::string_view fragmentEntryPoint{"main"};
        VkFormat colorFormat{VK_FORMAT_UNDEFINED};
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        std::span<const VkVertexInputBindingDescription> vertexBindings;
        std::span<const VkVertexInputAttributeDescription> vertexAttributes;
    };

    class VulkanGraphicsPipeline {
    public:
        VulkanGraphicsPipeline() = default;
        VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
        VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
        VulkanGraphicsPipeline(VulkanGraphicsPipeline&& other) noexcept;
        VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&& other) noexcept;
        ~VulkanGraphicsPipeline();

        [[nodiscard]] static Result<VulkanGraphicsPipeline>
        createDynamicRendering(const VulkanGraphicsPipelineDesc& desc);
        [[nodiscard]] VkPipeline handle() const;

    private:
        void destroy();

        VkDevice device_{VK_NULL_HANDLE};
        VkPipeline pipeline_{VK_NULL_HANDLE};
    };

} // namespace vke
