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

    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(
        VulkanDescriptorSetLayout&& other) noexcept {
        *this = std::move(other);
    }

    VulkanDescriptorSetLayout&
    VulkanDescriptorSetLayout::operator=(VulkanDescriptorSetLayout&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        descriptorSetLayout_ = std::exchange(other.descriptorSetLayout_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
        destroy();
    }

    void VulkanDescriptorSetLayout::destroy() {
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    Result<VulkanDescriptorSetLayout>
    VulkanDescriptorSetLayout::create(const VulkanDescriptorSetLayoutDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan descriptor set layout without a device")};
        }

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.flags = desc.flags;
        createInfo.bindingCount = static_cast<std::uint32_t>(desc.bindings.size());
        createInfo.pBindings = desc.bindings.data();

        VulkanDescriptorSetLayout descriptorSetLayout;
        descriptorSetLayout.device_ = desc.device;
        const VkResult result = vkCreateDescriptorSetLayout(
            desc.device, &createInfo, nullptr, &descriptorSetLayout.descriptorSetLayout_);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to create Vulkan descriptor set layout", result)};
        }

        return descriptorSetLayout;
    }

    VkDescriptorSetLayout VulkanDescriptorSetLayout::handle() const {
        return descriptorSetLayout_;
    }

    VulkanDescriptorPool::VulkanDescriptorPool(VulkanDescriptorPool&& other) noexcept {
        *this = std::move(other);
    }

    VulkanDescriptorPool&
    VulkanDescriptorPool::operator=(VulkanDescriptorPool&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        descriptorPool_ = std::exchange(other.descriptorPool_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanDescriptorPool::~VulkanDescriptorPool() {
        destroy();
    }

    void VulkanDescriptorPool::destroy() {
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        descriptorPool_ = VK_NULL_HANDLE;
    }

    Result<VulkanDescriptorPool>
    VulkanDescriptorPool::create(const VulkanDescriptorPoolDesc& desc) {
        if (desc.device == VK_NULL_HANDLE || desc.maxSets == 0 || desc.poolSizes.empty()) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan descriptor pool from incomplete inputs")};
        }

        std::vector<VkDescriptorPoolSize> poolSizes;
        poolSizes.reserve(desc.poolSizes.size());
        for (const VulkanDescriptorPoolSize& poolSize : desc.poolSizes) {
            if (poolSize.count == 0) {
                return std::unexpected{
                    vulkanError("Cannot create a Vulkan descriptor pool with an empty pool size")};
            }

            poolSizes.push_back(VkDescriptorPoolSize{
                .type = poolSize.type,
                .descriptorCount = poolSize.count,
            });
        }

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.flags = desc.flags;
        createInfo.maxSets = desc.maxSets;
        createInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();

        VulkanDescriptorPool descriptorPool;
        descriptorPool.device_ = desc.device;
        const VkResult result = vkCreateDescriptorPool(desc.device, &createInfo, nullptr,
                                                       &descriptorPool.descriptorPool_);
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to create Vulkan descriptor pool", result)};
        }

        return descriptorPool;
    }

    Result<std::vector<VkDescriptorSet>>
    VulkanDescriptorPool::allocate(const VulkanDescriptorSetAllocationDesc& desc) const {
        if (device_ == VK_NULL_HANDLE || descriptorPool_ == VK_NULL_HANDLE ||
            desc.setLayouts.empty()) {
            return std::unexpected{
                vulkanError("Cannot allocate Vulkan descriptor sets from incomplete inputs")};
        }

        std::vector<VkDescriptorSet> descriptorSets(desc.setLayouts.size(), VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool_;
        allocateInfo.descriptorSetCount = static_cast<std::uint32_t>(desc.setLayouts.size());
        allocateInfo.pSetLayouts = desc.setLayouts.data();

        const VkResult result = vkAllocateDescriptorSets(device_, &allocateInfo,
                                                         descriptorSets.data());
        if (result != VK_SUCCESS) {
            return std::unexpected{
                vulkanError("Failed to allocate Vulkan descriptor sets", result)};
        }

        return descriptorSets;
    }

    VkDescriptorPool VulkanDescriptorPool::handle() const {
        return descriptorPool_;
    }

    VulkanDescriptorAllocator::VulkanDescriptorAllocator(
        VulkanDescriptorAllocator&& other) noexcept {
        *this = std::move(other);
    }

    VulkanDescriptorAllocator&
    VulkanDescriptorAllocator::operator=(VulkanDescriptorAllocator&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        descriptorPool_ = std::move(other.descriptorPool_);
        stats_ = std::exchange(other.stats_, VulkanDescriptorAllocatorStats{});
        return *this;
    }

    Result<VulkanDescriptorAllocator>
    VulkanDescriptorAllocator::create(const VulkanDescriptorPoolDesc& desc) {
        auto descriptorPool = VulkanDescriptorPool::create(desc);
        if (!descriptorPool) {
            return std::unexpected{std::move(descriptorPool.error())};
        }

        VulkanDescriptorAllocator allocator;
        allocator.descriptorPool_ = std::move(*descriptorPool);
        allocator.stats_.poolsCreated = 1;
        return allocator;
    }

    Result<std::vector<VkDescriptorSet>>
    VulkanDescriptorAllocator::allocate(const VulkanDescriptorSetAllocationDesc& desc) {
        auto descriptorSets = descriptorPool_.allocate(desc);
        if (!descriptorSets) {
            return std::unexpected{std::move(descriptorSets.error())};
        }

        ++stats_.allocationCalls;
        stats_.setsAllocated += descriptorSets->size();
        return descriptorSets;
    }

    VulkanDescriptorAllocatorStats VulkanDescriptorAllocator::stats() const {
        return stats_;
    }

    VkDescriptorPool VulkanDescriptorAllocator::handle() const {
        return descriptorPool_.handle();
    }

    void updateVulkanDescriptorBuffers(
        VkDevice device, std::span<const VulkanDescriptorBufferWrite> writes) {
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        bufferInfos.reserve(writes.size());
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        descriptorWrites.reserve(writes.size());

        for (const VulkanDescriptorBufferWrite& write : writes) {
            bufferInfos.push_back(VkDescriptorBufferInfo{
                .buffer = write.buffer,
                .offset = write.offset,
                .range = write.range,
            });

            descriptorWrites.push_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = write.descriptorSet,
                .dstBinding = write.binding,
                .dstArrayElement = write.arrayElement,
                .descriptorCount = 1,
                .descriptorType = write.descriptorType,
                .pImageInfo = nullptr,
                .pBufferInfo = &bufferInfos.back(),
                .pTexelBufferView = nullptr,
            });
        }

        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    void updateVulkanDescriptorImages(
        VkDevice device, std::span<const VulkanDescriptorImageWrite> writes) {
        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(writes.size());
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        descriptorWrites.reserve(writes.size());

        for (const VulkanDescriptorImageWrite& write : writes) {
            imageInfos.push_back(VkDescriptorImageInfo{
                .sampler = write.sampler,
                .imageView = write.imageView,
                .imageLayout = write.imageLayout,
            });

            descriptorWrites.push_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = write.descriptorSet,
                .dstBinding = write.binding,
                .dstArrayElement = write.arrayElement,
                .descriptorCount = 1,
                .descriptorType = write.descriptorType,
                .pImageInfo = &imageInfos.back(),
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            });
        }

        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    VulkanPipelineCache::VulkanPipelineCache(VulkanPipelineCache&& other) noexcept {
        *this = std::move(other);
    }

    VulkanPipelineCache&
    VulkanPipelineCache::operator=(VulkanPipelineCache&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineCache_ = std::exchange(other.pipelineCache_, VK_NULL_HANDLE);
        return *this;
    }

    VulkanPipelineCache::~VulkanPipelineCache() {
        destroy();
    }

    void VulkanPipelineCache::destroy() {
        if (pipelineCache_ != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        }

        device_ = VK_NULL_HANDLE;
        pipelineCache_ = VK_NULL_HANDLE;
    }

    Result<VulkanPipelineCache> VulkanPipelineCache::create(const VulkanPipelineCacheDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan pipeline cache without a device")};
        }

        VkPipelineCacheCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        createInfo.flags = desc.flags;
        createInfo.initialDataSize = desc.initialData.size_bytes();
        createInfo.pInitialData = desc.initialData.empty() ? nullptr : desc.initialData.data();

        VulkanPipelineCache pipelineCache;
        pipelineCache.device_ = desc.device;
        const VkResult result = vkCreatePipelineCache(desc.device, &createInfo, nullptr,
                                                      &pipelineCache.pipelineCache_);
        if (result != VK_SUCCESS) {
            return std::unexpected{vulkanError("Failed to create Vulkan pipeline cache", result)};
        }

        return pipelineCache;
    }

    VkPipelineCache VulkanPipelineCache::handle() const {
        return pipelineCache_;
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
            desc.vertexEntryPoint.empty() || desc.fragmentEntryPoint.empty() ||
            desc.colorFormat == VK_FORMAT_UNDEFINED) {
            return std::unexpected{
                vulkanError("Cannot create a Vulkan graphics pipeline from incomplete inputs")};
        }

        const std::string vertexEntryPoint{desc.vertexEntryPoint};
        const std::string fragmentEntryPoint{desc.fragmentEntryPoint};

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = desc.vertexShader;
        stages[0].pName = vertexEntryPoint.c_str();
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = desc.fragmentShader;
        stages[1].pName = fragmentEntryPoint.c_str();

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

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable =
            desc.depthFormat != VK_FORMAT_UNDEFINED ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable =
            desc.depthFormat != VK_FORMAT_UNDEFINED ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

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
        renderingInfo.depthAttachmentFormat = desc.depthFormat;

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
        createInfo.pDepthStencilState = &depthStencil;
        createInfo.pColorBlendState = &colorBlend;
        createInfo.pDynamicState = &dynamicState;
        createInfo.layout = desc.layout;

        VulkanGraphicsPipeline pipeline;
        pipeline.device_ = desc.device;
        const VkResult result = vkCreateGraphicsPipelines(
            desc.device, desc.pipelineCache, 1, &createInfo, nullptr, &pipeline.pipeline_);
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
