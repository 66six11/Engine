#include "vke/rhi_vulkan/vulkan_pipeline.hpp"

#include <array>
#include <string>
#include <utility>

#include "vke/rhi_vulkan/vulkan_error.hpp"

namespace vke {
    VulkanShaderModule::VulkanShaderModule(VulkanShaderModule&& other) noexcept {
        *this = std::move(other);
    }

    VulkanShaderModule& VulkanShaderModule::operator=(VulkanShaderModule&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        shaderModule_ = std::exchange(other.shaderModule_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanShaderModule::~VulkanShaderModule() {
        destroy();
    }

    void VulkanShaderModule::destroy() {
        if (shaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, shaderModule_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        shaderModule_ = VK_NULL_HANDLE;
    }

    Result<VulkanShaderModule> VulkanShaderModule::create(const VulkanShaderModuleDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan shader module without a device")};
        }
        if (desc.code.empty()) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan shader module without SPIR-V")};
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = desc.code.size_bytes();
        createInfo.pCode = desc.code.data();

        VulkanShaderModule shaderModule;
        shaderModule.device_ = desc.device;
        const auto createShaderModule = [&]() -> VkResult {
            return vkCreateShaderModule(desc.device, &createInfo, nullptr,
                                        &shaderModule.shaderModule_);
        };
        const VkResult result = createShaderModule();
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan shader module", result)};
        }

        return shaderModule;
    }

    VkShaderModule VulkanShaderModule::handle() const {
        return shaderModule_;
    }

    VulkanPipelineLayout::VulkanPipelineLayout(VulkanPipelineLayout&& other) noexcept {
        *this = std::move(other);
    }

    VulkanPipelineLayout& VulkanPipelineLayout::operator=(VulkanPipelineLayout&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanPipelineLayout::~VulkanPipelineLayout() {
        destroy();
    }

    void VulkanPipelineLayout::destroy() {
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
    }

    Result<VulkanPipelineLayout>
    VulkanPipelineLayout::create(const VulkanPipelineLayoutDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan pipeline layout without a device")};
        }

        VkPipelineLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        createInfo.setLayoutCount = static_cast<std::uint32_t>(desc.setLayouts.size());
        createInfo.pSetLayouts = desc.setLayouts.data();
        createInfo.pushConstantRangeCount =
            static_cast<std::uint32_t>(desc.pushConstantRanges.size());
        createInfo.pPushConstantRanges = desc.pushConstantRanges.data();

        VulkanPipelineLayout pipelineLayout;
        pipelineLayout.device_ = desc.device;
        const VkResult result = vkCreatePipelineLayout(desc.device, &createInfo, nullptr,
                                                       &pipelineLayout.pipelineLayout_);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan pipeline layout", result)};
        }

        return pipelineLayout;
    }

    VkPipelineLayout VulkanPipelineLayout::handle() const {
        return pipelineLayout_;
    }

    VulkanGraphicsPipeline::VulkanGraphicsPipeline(VulkanGraphicsPipeline&& other) noexcept {
        *this = std::move(other);
    }

    VulkanGraphicsPipeline&
    VulkanGraphicsPipeline::operator=(VulkanGraphicsPipeline&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
        destroy();
    }

    void VulkanGraphicsPipeline::destroy() {
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
    }

    Result<VulkanGraphicsPipeline>
    VulkanGraphicsPipeline::createDynamicRendering(const VulkanGraphicsPipelineDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.layout == VK_NULL_HANDLE ||
            desc.vertexShader == VK_NULL_HANDLE || desc.fragmentShader == VK_NULL_HANDLE ||
            desc.colorFormat == VK_FORMAT_UNDEFINED) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan graphics pipeline from incomplete inputs")};
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = desc.vertexShader;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = desc.fragmentShader;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount =
            static_cast<std::uint32_t>(desc.vertexBindings.size());
        vertexInput.pVertexBindingDescriptions = desc.vertexBindings.data();
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(desc.vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions = desc.vertexAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0F;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &colorBlendAttachment;

        constexpr std::array dynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &desc.colorFormat;

        VkGraphicsPipelineCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.pNext = &renderingInfo;
        createInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        createInfo.pStages = stages.data();
        createInfo.pVertexInputState = &vertexInput;
        createInfo.pInputAssemblyState = &inputAssembly;
        createInfo.pViewportState = &viewportState;
        createInfo.pRasterizationState = &rasterization;
        createInfo.pMultisampleState = &multisample;
        createInfo.pColorBlendState = &colorBlend;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = desc.layout;

        VulkanGraphicsPipeline pipeline;
        pipeline.device_ = desc.device;
        const VkResult result = vkCreateGraphicsPipelines(
            desc.device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline.pipeline_);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to create Vulkan graphics pipeline", result)};
        }

        return pipeline;
    }

    VkPipeline VulkanGraphicsPipeline::handle() const {
        return pipeline_;
    }

} // namespace vke
