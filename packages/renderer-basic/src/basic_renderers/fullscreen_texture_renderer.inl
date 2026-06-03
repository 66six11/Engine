BasicFullscreenTextureRenderer::BasicFullscreenTextureRenderer(
    BasicFullscreenTextureRenderer&& other) noexcept {
    *this = std::move(other);
}

BasicFullscreenTextureRenderer&
BasicFullscreenTextureRenderer::operator=(BasicFullscreenTextureRenderer&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    allocator_ = std::exchange(other.allocator_, nullptr);
    vertexShader_ = std::move(other.vertexShader_);
    fragmentShader_ = std::move(other.fragmentShader_);
    worldGridVertexShader_ = std::move(other.worldGridVertexShader_);
    worldGridFragmentShader_ = std::move(other.worldGridFragmentShader_);
    debugLineVertexShader_ = std::move(other.debugLineVertexShader_);
    debugLineFragmentShader_ = std::move(other.debugLineFragmentShader_);
    descriptorSetLayouts_ = std::move(other.descriptorSetLayouts_);
    pipelineLayout_ = std::move(other.pipelineLayout_);
    worldGridPipelineLayout_ = std::move(other.worldGridPipelineLayout_);
    debugLinePipelineLayout_ = std::move(other.debugLinePipelineLayout_);
    pipelineCache_ = std::move(other.pipelineCache_);
    pipeline_ = std::move(other.pipeline_);
    worldGridPipeline_ = std::move(other.worldGridPipeline_);
    debugLinePipeline_ = std::move(other.debugLinePipeline_);
    pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
    worldGridPipelineFormat_ =
        std::exchange(other.worldGridPipelineFormat_, VK_FORMAT_UNDEFINED);
    debugLinePipelineFormat_ =
        std::exchange(other.debugLinePipelineFormat_, VK_FORMAT_UNDEFINED);
    worldGridPipelineBlendMode_ = std::exchange(
        other.worldGridPipelineBlendMode_, BasicRenderViewOverlayBlendMode::AlphaBlend);
    debugLinePipelineBlendMode_ = std::exchange(
        other.debugLinePipelineBlendMode_, BasicRenderViewOverlayBlendMode::AlphaBlend);
    pipelineCacheStats_ = std::exchange(other.pipelineCacheStats_, {});
    offscreenViewportTarget_ = std::move(other.offscreenViewportTarget_);
    descriptorAllocator_ = std::move(other.descriptorAllocator_);
    descriptorSets_ = std::move(other.descriptorSets_);
    compositeDescriptorSets_ = std::move(other.compositeDescriptorSets_);
    descriptorSetEpoch_ = std::exchange(other.descriptorSetEpoch_, 0);
    compositeDescriptorSetEpoch_ = std::exchange(other.compositeDescriptorSetEpoch_, 0);
    descriptorSetCursor_ = std::exchange(other.descriptorSetCursor_, 0);
    compositeDescriptorSetCursor_ = std::exchange(other.compositeDescriptorSetCursor_, 0);
    debugLineVertexBuffers_ = std::move(other.debugLineVertexBuffers_);
    debugLineVertexBufferSizes_ = std::move(other.debugLineVertexBufferSizes_);
    debugLineVertexBufferEpoch_ = std::exchange(other.debugLineVertexBufferEpoch_, 0);
    debugLineVertexBufferCursor_ = std::exchange(other.debugLineVertexBufferCursor_, 0);
    uniformBuffer_ = std::move(other.uniformBuffer_);
    sampler_ = std::move(other.sampler_);
    transientImagePool_ = std::move(other.transientImagePool_);
    transientImages_ = std::move(other.transientImages_);
    return *this;
}

Result<BasicFullscreenTextureRenderer>
BasicFullscreenTextureRenderer::create(const BasicFullscreenTextureRendererDesc& desc) {
    if (desc.device == VK_NULL_HANDLE) {
        return std::unexpected{Error{ErrorDomain::Vulkan, 0,
                                     "Cannot create fullscreen texture renderer without a device"}};
    }
    if (desc.allocator == nullptr) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Cannot create fullscreen texture renderer without an allocator"}};
    }

    auto signature = validateFullscreenTextureReflection(desc.shaderDirectory);
    if (!signature) {
        return std::unexpected{std::move(signature.error())};
    }
    auto debugLineReflection = validateDebugLineReflection(desc.shaderDirectory);
    if (!debugLineReflection) {
        return std::unexpected{std::move(debugLineReflection.error())};
    }
    auto worldGridReflection = validateWorldGridReflection(desc.shaderDirectory);
    if (!worldGridReflection) {
        return std::unexpected{std::move(worldGridReflection.error())};
    }
    auto resources = createPipelineLayoutResources(desc.device, *signature);
    if (!resources) {
        return std::unexpected{std::move(resources.error())};
    }
    if (resources->descriptorSetLayouts.empty()) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Fullscreen texture renderer produced no descriptor set layout"}};
    }

    auto vertexCode = readSpirvFile(desc.shaderDirectory / "descriptor_layout.vert.spv");
    if (!vertexCode) {
        return std::unexpected{std::move(vertexCode.error())};
    }
    auto fragmentCode = readSpirvFile(desc.shaderDirectory / "descriptor_layout.frag.spv");
    if (!fragmentCode) {
        return std::unexpected{std::move(fragmentCode.error())};
    }
    auto worldGridVertexCode = readSpirvFile(desc.shaderDirectory / "world_grid.vert.spv");
    if (!worldGridVertexCode) {
        return std::unexpected{std::move(worldGridVertexCode.error())};
    }
    auto worldGridFragmentCode = readSpirvFile(desc.shaderDirectory / "world_grid.frag.spv");
    if (!worldGridFragmentCode) {
        return std::unexpected{std::move(worldGridFragmentCode.error())};
    }
    auto debugLineVertexCode = readSpirvFile(desc.shaderDirectory / "debug_line.vert.spv");
    if (!debugLineVertexCode) {
        return std::unexpected{std::move(debugLineVertexCode.error())};
    }
    auto debugLineFragmentCode = readSpirvFile(desc.shaderDirectory / "debug_line.frag.spv");
    if (!debugLineFragmentCode) {
        return std::unexpected{std::move(debugLineFragmentCode.error())};
    }

    auto vertexShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *vertexCode,
    });
    if (!vertexShader) {
        return std::unexpected{std::move(vertexShader.error())};
    }
    auto fragmentShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *fragmentCode,
    });
    if (!fragmentShader) {
        return std::unexpected{std::move(fragmentShader.error())};
    }
    auto worldGridVertexShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *worldGridVertexCode,
    });
    if (!worldGridVertexShader) {
        return std::unexpected{std::move(worldGridVertexShader.error())};
    }
    auto worldGridFragmentShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *worldGridFragmentCode,
    });
    if (!worldGridFragmentShader) {
        return std::unexpected{std::move(worldGridFragmentShader.error())};
    }
    auto debugLineVertexShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *debugLineVertexCode,
    });
    if (!debugLineVertexShader) {
        return std::unexpected{std::move(debugLineVertexShader.error())};
    }
    auto debugLineFragmentShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *debugLineFragmentCode,
    });
    if (!debugLineFragmentShader) {
        return std::unexpected{std::move(debugLineFragmentShader.error())};
    }
    constexpr std::array worldGridPushConstantRanges{
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size =
                static_cast<std::uint32_t>(sizeof(BasicRenderViewWorldGridPushConstants)),
        },
    };
    auto worldGridPipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
        .device = desc.device,
        .setLayouts = {},
        .pushConstantRanges = worldGridPushConstantRanges,
    });
    if (!worldGridPipelineLayout) {
        return std::unexpected{std::move(worldGridPipelineLayout.error())};
    }
    auto debugLinePipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
        .device = desc.device,
        .setLayouts = {},
        .pushConstantRanges = {},
    });
    if (!debugLinePipelineLayout) {
        return std::unexpected{std::move(debugLinePipelineLayout.error())};
    }

    constexpr std::array tint{1.0F, 1.0F, 1.0F, 1.0F};
    auto uniformBuffer = VulkanBuffer::create(VulkanBufferDesc{
        .device = desc.device,
        .allocator = desc.allocator,
        .size = sizeof(tint),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
    });
    if (!uniformBuffer) {
        return std::unexpected{std::move(uniformBuffer.error())};
    }
    auto uploaded = uniformBuffer->upload(std::as_bytes(std::span{tint}));
    if (!uploaded) {
        return std::unexpected{std::move(uploaded.error())};
    }

    auto sampler = VulkanSampler::create(VulkanSamplerDesc{.device = desc.device});
    if (!sampler) {
        return std::unexpected{std::move(sampler.error())};
    }
    auto pipelineCache = createBasicPipelineCache(desc.device);
    if (!pipelineCache) {
        return std::unexpected{std::move(pipelineCache.error())};
    }

    constexpr std::size_t kDescriptorSetRingSize = 16;
    constexpr std::size_t kDebugLineVertexBufferRingSize = 16;
    constexpr std::uint32_t kDescriptorSetCount =
        static_cast<std::uint32_t>(kDescriptorSetRingSize * 2U);
    constexpr std::array poolSizes{
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .count = kDescriptorSetCount,
        },
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .count = kDescriptorSetCount,
        },
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .count = kDescriptorSetCount,
        },
    };
    auto descriptorAllocator = VulkanDescriptorAllocator::create(VulkanDescriptorPoolDesc{
        .device = desc.device,
        .maxSets = kDescriptorSetCount,
        .poolSizes = poolSizes,
    });
    if (!descriptorAllocator) {
        return std::unexpected{std::move(descriptorAllocator.error())};
    }

    std::vector<VkDescriptorSetLayout> setLayouts(kDescriptorSetCount,
                                                  resources->descriptorSetLayouts.front().handle());
    auto descriptorSets = descriptorAllocator->allocate(VulkanDescriptorSetAllocationDesc{
        .setLayouts = setLayouts,
    });
    if (!descriptorSets) {
        return std::unexpected{std::move(descriptorSets.error())};
    }
    if (descriptorSets->size() != kDescriptorSetCount ||
        std::ranges::any_of(*descriptorSets,
                            [](VkDescriptorSet set) { return set == VK_NULL_HANDLE; })) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Fullscreen texture renderer failed to allocate descriptor set ring"}};
    }

    std::vector<VulkanDescriptorBufferWrite> bufferWrites;
    bufferWrites.reserve(descriptorSets->size());
    std::vector<VulkanDescriptorImageWrite> samplerWrites;
    samplerWrites.reserve(descriptorSets->size());
    for (VkDescriptorSet descriptorSet : *descriptorSets) {
        bufferWrites.push_back(VulkanDescriptorBufferWrite{
            .descriptorSet = descriptorSet,
            .binding = 0,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .buffer = uniformBuffer->handle(),
            .offset = 0,
            .range = uniformBuffer->size(),
        });
        samplerWrites.push_back(VulkanDescriptorImageWrite{
            .descriptorSet = descriptorSet,
            .binding = 2,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .imageView = VK_NULL_HANDLE,
            .sampler = sampler->handle(),
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        });
    }
    updateVulkanDescriptorBuffers(desc.device, bufferWrites);
    updateVulkanDescriptorImages(desc.device, samplerWrites);

    std::vector<VkDescriptorSet> fullscreenDescriptorSets;
    fullscreenDescriptorSets.reserve(kDescriptorSetRingSize);
    std::vector<VkDescriptorSet> compositeDescriptorSets;
    compositeDescriptorSets.reserve(kDescriptorSetRingSize);
    for (std::size_t index = 0; index < kDescriptorSetRingSize; ++index) {
        fullscreenDescriptorSets.push_back((*descriptorSets)[index]);
        compositeDescriptorSets.push_back((*descriptorSets)[index + kDescriptorSetRingSize]);
    }

    BasicFullscreenTextureRenderer renderer;
    renderer.device_ = desc.device;
    renderer.allocator_ = desc.allocator;
    renderer.vertexShader_ = std::move(*vertexShader);
    renderer.fragmentShader_ = std::move(*fragmentShader);
    renderer.worldGridVertexShader_ = std::move(*worldGridVertexShader);
    renderer.worldGridFragmentShader_ = std::move(*worldGridFragmentShader);
    renderer.debugLineVertexShader_ = std::move(*debugLineVertexShader);
    renderer.debugLineFragmentShader_ = std::move(*debugLineFragmentShader);
    renderer.descriptorSetLayouts_ = std::move(resources->descriptorSetLayouts);
    renderer.pipelineLayout_ = std::move(resources->pipelineLayout);
    renderer.worldGridPipelineLayout_ = std::move(*worldGridPipelineLayout);
    renderer.debugLinePipelineLayout_ = std::move(*debugLinePipelineLayout);
    renderer.pipelineCache_ = std::move(*pipelineCache);
    renderer.descriptorAllocator_ = std::move(*descriptorAllocator);
    renderer.descriptorSets_ = std::move(fullscreenDescriptorSets);
    renderer.compositeDescriptorSets_ = std::move(compositeDescriptorSets);
    renderer.debugLineVertexBuffers_.resize(kDebugLineVertexBufferRingSize);
    renderer.debugLineVertexBufferSizes_.resize(kDebugLineVertexBufferRingSize);
    renderer.uniformBuffer_ = std::move(*uniformBuffer);
    renderer.sampler_ = std::move(*sampler);
    return renderer;
}

Result<void> BasicFullscreenTextureRenderer::ensurePipeline(VkFormat colorFormat) {
    if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat) {
        ++pipelineCacheStats_.reused;
        return {};
    }

    auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
        .device = device_,
        .pipelineCache = pipelineCache_.handle(),
        .layout = pipelineLayout_.handle(),
        .vertexShader = vertexShader_.handle(),
        .fragmentShader = fragmentShader_.handle(),
        .vertexEntryPoint = "main",
        .fragmentEntryPoint = "main",
        .colorFormat = colorFormat,
        .vertexBindings = {},
        .vertexAttributes = {},
    });
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }

    pipeline_ = std::move(*pipeline);
    pipelineFormat_ = colorFormat;
    ++pipelineCacheStats_.created;
    return {};
}

Result<void> BasicFullscreenTextureRenderer::ensureWorldGridPipeline(
    VkFormat colorFormat, BasicRenderViewOverlayBlendMode blendMode) {
    if (worldGridPipeline_.handle() != VK_NULL_HANDLE &&
        worldGridPipelineFormat_ == colorFormat && worldGridPipelineBlendMode_ == blendMode) {
        return {};
    }

    VkBlendFactor colorSrcBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor colorDstBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendFactor alphaSrcBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor alphaDstBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    switch (blendMode) {
    case BasicRenderViewOverlayBlendMode::AlphaBlend:
        break;
    case BasicRenderViewOverlayBlendMode::Additive:
        colorDstBlendFactor = VK_BLEND_FACTOR_ONE;
        alphaDstBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }

    auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
        .device = device_,
        .pipelineCache = pipelineCache_.handle(),
        .layout = worldGridPipelineLayout_.handle(),
        .vertexShader = worldGridVertexShader_.handle(),
        .fragmentShader = worldGridFragmentShader_.handle(),
        .vertexEntryPoint = "main",
        .fragmentEntryPoint = "main",
        .colorFormat = colorFormat,
        .depthFormat = VK_FORMAT_UNDEFINED,
        .vertexBindings = {},
        .vertexAttributes = {},
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .colorBlendEnable = VK_TRUE,
        .colorSrcBlendFactor = colorSrcBlendFactor,
        .colorDstBlendFactor = colorDstBlendFactor,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .alphaSrcBlendFactor = alphaSrcBlendFactor,
        .alphaDstBlendFactor = alphaDstBlendFactor,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    });
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }

    worldGridPipeline_ = std::move(*pipeline);
    worldGridPipelineFormat_ = colorFormat;
    worldGridPipelineBlendMode_ = blendMode;
    return {};
}

Result<void> BasicFullscreenTextureRenderer::ensureDebugLinePipeline(
    VkFormat colorFormat, BasicRenderViewOverlayBlendMode blendMode) {
    if (debugLinePipeline_.handle() != VK_NULL_HANDLE &&
        debugLinePipelineFormat_ == colorFormat && debugLinePipelineBlendMode_ == blendMode) {
        return {};
    }

    const auto bindings = basicDebugLineVertexInputBindings();
    const auto attributes = basicDebugLineVertexInputAttributes();
    VkBlendFactor colorSrcBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor colorDstBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendFactor alphaSrcBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor alphaDstBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    switch (blendMode) {
    case BasicRenderViewOverlayBlendMode::AlphaBlend:
        break;
    case BasicRenderViewOverlayBlendMode::Additive:
        colorDstBlendFactor = VK_BLEND_FACTOR_ONE;
        alphaDstBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }

    auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
        .device = device_,
        .pipelineCache = pipelineCache_.handle(),
        .layout = debugLinePipelineLayout_.handle(),
        .vertexShader = debugLineVertexShader_.handle(),
        .fragmentShader = debugLineFragmentShader_.handle(),
        .vertexEntryPoint = "main",
        .fragmentEntryPoint = "main",
        .colorFormat = colorFormat,
        .depthFormat = VK_FORMAT_UNDEFINED,
        .vertexBindings = bindings,
        .vertexAttributes = attributes,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .colorBlendEnable = VK_TRUE,
        .colorSrcBlendFactor = colorSrcBlendFactor,
        .colorDstBlendFactor = colorDstBlendFactor,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .alphaSrcBlendFactor = alphaSrcBlendFactor,
        .alphaDstBlendFactor = alphaDstBlendFactor,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    });
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }

    debugLinePipeline_ = std::move(*pipeline);
    debugLinePipelineFormat_ = colorFormat;
    debugLinePipelineBlendMode_ = blendMode;
    return {};
}

BasicPipelineCacheStats BasicFullscreenTextureRenderer::pipelineCacheStats() const {
    return pipelineCacheStats_;
}

VkDescriptorSet BasicFullscreenTextureRenderer::acquireFullscreenDescriptorSet(
    const VulkanFrameRecordContext& frame) {
    const std::uint64_t epoch =
        frame.frameLoop == nullptr ? 0U : frame.frameLoop->submittedFrameEpoch() + 1U;
    if (descriptorSetEpoch_ != epoch) {
        descriptorSetEpoch_ = epoch;
        descriptorSetCursor_ = 0;
    }
    if (descriptorSetCursor_ >= descriptorSets_.size()) {
        return VK_NULL_HANDLE;
    }
    return descriptorSets_[descriptorSetCursor_++];
}

VkDescriptorSet BasicFullscreenTextureRenderer::acquireCompositeDescriptorSet(
    const VulkanFrameRecordContext& frame) {
    const std::uint64_t epoch =
        frame.frameLoop == nullptr ? 0U : frame.frameLoop->submittedFrameEpoch() + 1U;
    if (compositeDescriptorSetEpoch_ != epoch) {
        compositeDescriptorSetEpoch_ = epoch;
        compositeDescriptorSetCursor_ = 0;
    }
    if (compositeDescriptorSetCursor_ >= compositeDescriptorSets_.size()) {
        return VK_NULL_HANDLE;
    }
    return compositeDescriptorSets_[compositeDescriptorSetCursor_++];
}

Result<VkBuffer> BasicFullscreenTextureRenderer::uploadDebugLineVertices(
    const VulkanFrameRecordContext& frame, std::span<const std::byte> vertices) {
    if (vertices.empty()) {
        return VK_NULL_HANDLE;
    }
    const std::uint64_t epoch =
        frame.frameLoop == nullptr ? 0U : frame.frameLoop->submittedFrameEpoch() + 1U;
    if (debugLineVertexBufferEpoch_ != epoch) {
        debugLineVertexBufferEpoch_ = epoch;
        debugLineVertexBufferCursor_ = 0;
    }
    if (debugLineVertexBufferCursor_ >= debugLineVertexBuffers_.size()) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Fullscreen texture renderer exhausted per-frame debug line vertex buffer ring"}};
    }

    VulkanBuffer& buffer = debugLineVertexBuffers_[debugLineVertexBufferCursor_];
    VkDeviceSize& bufferSize = debugLineVertexBufferSizes_[debugLineVertexBufferCursor_];
    ++debugLineVertexBufferCursor_;

    const auto requiredSize = static_cast<VkDeviceSize>(vertices.size_bytes());
    if (buffer.handle() == VK_NULL_HANDLE || bufferSize < requiredSize) {
        auto created = VulkanBuffer::create(VulkanBufferDesc{
            .device = device_,
            .allocator = allocator_,
            .size = requiredSize,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!created) {
            return std::unexpected{std::move(created.error())};
        }
        buffer = std::move(*created);
        bufferSize = requiredSize;
    }

    auto uploaded = buffer.upload(vertices);
    if (!uploaded) {
        return std::unexpected{std::move(uploaded.error())};
    }
    return buffer.handle();
}

BasicOffscreenViewportStats BasicFullscreenTextureRenderer::offscreenViewportStats() const {
    const VulkanRenderTargetStats stats = offscreenViewportTarget_.stats();
    return BasicOffscreenViewportStats{
        .renderTargetsCreated = stats.created,
        .renderTargetsReused = stats.reused,
        .renderTargetsDeferredForDeletion = stats.deferredForDeletion,
    };
}

BasicOffscreenViewportTarget BasicFullscreenTextureRenderer::offscreenViewportTarget() const {
    const VulkanSampledTextureView target = offscreenViewportTarget_.sampledTextureView();
    return BasicOffscreenViewportTarget{
        .image = target.image,
        .imageView = target.imageView,
        .format = target.format,
        .extent = target.extent,
        .sampledLayout = target.sampledLayout,
    };
}

VulkanDescriptorAllocatorStats BasicFullscreenTextureRenderer::descriptorAllocatorStats() const {
    return descriptorAllocator_.stats();
}

VulkanBufferStats BasicFullscreenTextureRenderer::bufferStats() const {
    VulkanBufferStats stats;
    accumulateBufferStats(stats, uniformBuffer_);
    for (const VulkanBuffer& buffer : debugLineVertexBuffers_) {
        accumulateBufferStats(stats, buffer);
    }
    return stats;
}

Result<void>
BasicFullscreenTextureRenderer::ensureOffscreenViewportTarget(const VulkanFrameRecordContext& frame,
                                                              VkFormat format, VkExtent2D extent) {
    return offscreenViewportTarget_.ensure(
        frame, VulkanRenderTargetDesc{
                   .device = device_,
                   .allocator = allocator_,
                   .format = format,
                   .extent = extent,
                   .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               });
}

Result<VulkanFrameRecordResult>
BasicFullscreenTextureRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
    BasicRenderViewDesc view;
    view.target = basicSwapchainRenderViewTarget(frame);
    return recordViewFrame(frame, view);
}

Result<VulkanFrameRecordResult>
BasicFullscreenTextureRenderer::recordViewFrame(const VulkanFrameRecordContext& frame,
                                                BasicRenderViewDesc view) {
    auto target = validateBasicRenderViewTarget(view.target, "Fullscreen render view");
    if (!target) {
        return std::unexpected{std::move(target.error())};
    }
    auto targetFormat =
        basicRenderGraphImageFormat(view.target.format, "Fullscreen render view target");
    if (!targetFormat) {
        return std::unexpected{std::move(targetFormat.error())};
    }

    auto pipeline = ensurePipeline(view.target.format);
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }
    auto sceneInputsValidated = validateBasicRenderViewSceneInputs(view);
    if (!sceneInputsValidated) {
        return std::unexpected{std::move(sceneInputsValidated.error())};
    }
    const VkDescriptorSet fullscreenDescriptorSet = acquireFullscreenDescriptorSet(frame);
    if (fullscreenDescriptorSet == VK_NULL_HANDLE) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Fullscreen texture renderer exhausted per-frame descriptor set ring"}};
    }
    const BasicRenderViewTarget viewTarget = view.target;
    BasicRenderViewExecutionEventRecorder eventRecorder;

    RenderGraph graph;
    auto renderTargetDesc =
        basicRenderViewTargetDesc(viewTarget, RenderGraphImageState::Undefined, "RenderViewTarget");
    if (!renderTargetDesc) {
        return std::unexpected{std::move(renderTargetDesc.error())};
    }
    const auto renderTarget = graph.importImage(*renderTargetDesc);
    const RenderGraphImageDesc sourceDesc{
        .name = "FullscreenSource",
        .format = *targetFormat,
        .extent = basicRenderGraphExtent(viewTarget.extent),
    };
    const auto source = graph.createTransientImage(sourceDesc);

    std::vector<VulkanRenderGraphImageBinding> bindings;
    bindings.reserve(3);
    bindings.push_back(basicRenderViewTargetBinding(renderTarget, viewTarget));

    const std::array debugPreviewCandidates{
        BasicRenderViewImageCandidate{
            .image = renderTarget,
            .name = "RenderViewTarget",
            .format = *targetFormat,
            .extent = basicRenderGraphExtent(viewTarget.extent),
            .aspectMask = viewTarget.aspectMask,
        },
        BasicRenderViewImageCandidate{
            .image = source,
            .name = sourceDesc.name,
            .format = sourceDesc.format,
            .extent = sourceDesc.extent,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        },
    };
    BasicDebugPreviewSourcePassCursor debugPreviewCursor{
        .graph = graph,
        .request = view.debugPreview,
        .candidates = debugPreviewCandidates,
        .bindings = bindings,
        .frame = frame,
        .eventRecorder = eventRecorder,
    };

    constexpr BasicTransferClearParams kClearParams{
        .color = {0.12F, 0.12F, 0.13F, 1.0F},
    };
    constexpr BasicFullscreenParams kFullscreenParams{
        .tint = {1.0F, 1.0F, 1.0F, 1.0F},
    };

    std::vector<BasicDebugWorldLine> debugWorldLines(view.overlay.debugWorldLines.begin(),
                                                     view.overlay.debugWorldLines.end());
    view.overlay.debugWorldLines = std::span<const BasicDebugWorldLine>{debugWorldLines};
    const BasicRenderViewPassPolicy renderViewPassPolicy =
        basicRenderViewPassPolicy(view, debugWorldLines);
    BasicRenderViewPassRecordingContext renderViewRecording{
        .graph = graph,
        .renderTarget = renderTarget,
        .policy = renderViewPassPolicy,
        .frame = frame,
        .bindings = bindings,
        .viewTarget = viewTarget,
        .camera = view.camera,
        .colorLoadOp = view.overlay.colorLoadOp,
        .colorStoreOp = view.overlay.colorStoreOp,
        .eventRecorder = eventRecorder,
    };
    if (renderViewPassPolicy.worldGridEnabled) {
        auto worldGridPipeline =
            ensureWorldGridPipeline(view.target.format, view.overlay.blendMode);
        if (!worldGridPipeline) {
            return std::unexpected{std::move(worldGridPipeline.error())};
        }
    }
    std::vector<BasicDebugLineVertex> debugLineVertices;
    VkBuffer debugLineVertexBuffer = VK_NULL_HANDLE;
    std::uint32_t debugLineVertexCount = 0;
    if (renderViewPassPolicy.debugLineOverlayEnabled) {
        auto debugLinePipeline =
            ensureDebugLinePipeline(view.target.format, view.overlay.blendMode);
        if (!debugLinePipeline) {
            return std::unexpected{std::move(debugLinePipeline.error())};
        }
        debugLineVertices = basicDebugLineVertices(view.camera, debugWorldLines);
        if (!debugLineVertices.empty()) {
            if (debugLineVertices.size() > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected{
                    Error{ErrorDomain::Vulkan, 0,
                          "RenderView debug line vertex count exceeds Vulkan draw limits"}};
            }
            debugLineVertexCount = static_cast<std::uint32_t>(debugLineVertices.size());
            auto uploadedDebugLines =
                uploadDebugLineVertices(frame, std::as_bytes(std::span{debugLineVertices}));
            if (!uploadedDebugLines) {
                return std::unexpected{std::move(uploadedDebugLines.error())};
            }
            debugLineVertexBuffer = *uploadedDebugLines;
        }
    }

    if (renderViewPassPolicy.sceneInputsEnabled) {
        debugPreviewCursor.advanceSourcePassWithoutPreview();
        addBasicRenderViewSceneInputsPass(renderViewRecording);
    }
    graph.addPass("ClearFullscreenSource", kBasicTransferClearPassType)
        .setParams(kBasicTransferClearParamsType, kClearParams)
        .writeTransfer("target", source)
        .recordCommands([kClearParams](RenderGraphCommandList& commands) {
            commands.clearColor("target", kClearParams.color);
        })
        .execute([&frame, &bindings, &eventRecorder](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicFullscreenSourceClear(frame, pass, bindings, &eventRecorder);
        });
    auto debugPreviewAfterClear = debugPreviewCursor.tryAddPreviewAfterSourcePass();
    if (!debugPreviewAfterClear) {
        return std::unexpected{std::move(debugPreviewAfterClear.error())};
    }

    graph.addPass("FullscreenTexture", kBasicRasterFullscreenPassType)
        .setParams(kBasicRasterFullscreenParamsType, kFullscreenParams)
        .readTexture("source", source, RenderGraphShaderStage::Fragment)
        .writeColor("target", renderTarget)
        .recordCommands([kFullscreenParams](RenderGraphCommandList& commands) {
            commands.setShader("Hidden/DescriptorLayout", "Fullscreen")
                .setTexture("SourceTex", "source")
                .setVec4("Tint", kFullscreenParams.tint)
                .drawFullscreenTriangle();
        })
        .execute([&frame, &bindings, viewTarget, &eventRecorder, fullscreenDescriptorSet,
                  this](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicFullscreenTexturePass(
                frame, pass, bindings, device_, pipeline_.handle(), pipelineLayout_.handle(),
                fullscreenDescriptorSet, viewTarget.extent,
                BasicFullscreenTexturePassMessages{
                    .paramsContext = "Fullscreen texture pass",
                    .unknownTextureSlotMessage =
                        "Fullscreen pipeline key references an unknown texture slot",
                },
                &eventRecorder);
        });
    auto debugPreviewAfterFullscreen = debugPreviewCursor.tryAddPreviewAfterSourcePass();
    if (!debugPreviewAfterFullscreen) {
        return std::unexpected{std::move(debugPreviewAfterFullscreen.error())};
    }

    if (renderViewPassPolicy.worldGridEnabled) {
        addBasicRenderViewWorldGridPass(
            renderViewRecording, worldGridPipeline_.handle(), worldGridPipelineLayout_.handle());
        auto debugPreviewAfterWorldGrid = debugPreviewCursor.tryAddPreviewAfterSourcePass();
        if (!debugPreviewAfterWorldGrid) {
            return std::unexpected{std::move(debugPreviewAfterWorldGrid.error())};
        }
    }

    if (renderViewPassPolicy.debugLineOverlayEnabled) {
        addBasicRenderViewOverlayPass(
            renderViewRecording, debugLinePipeline_.handle(), debugLineVertexBuffer,
            debugLineVertexCount);
        auto debugPreviewAfterOverlay = debugPreviewCursor.tryAddPreviewAfterSourcePass();
        if (!debugPreviewAfterOverlay) {
            return std::unexpected{std::move(debugPreviewAfterOverlay.error())};
        }
    }

    auto debugPreviewAtEnd = debugPreviewCursor.tryAddEndOfGraphPreview();
    if (!debugPreviewAtEnd) {
        return std::unexpected{std::move(debugPreviewAtEnd.error())};
    }
    debugPreviewCursor.markUnrecordedSelectedPassUnavailable();

    const RenderGraphSchemaRegistry schemas = basicRenderGraphSchemaRegistry();
    auto compiled = graph.compile(schemas);
    if (!compiled) {
        return std::unexpected{std::move(compiled.error())};
    }

    auto prepared = prepareTransientResources(frame, device_, allocator_, *compiled, bindings,
                                              transientImagePool_, transientImages_);
    if (!prepared) {
        return std::unexpected{std::move(prepared.error())};
    }

    auto executed = graph.execute(*compiled);
    if (!executed) {
        return std::unexpected{std::move(executed.error())};
    }

    auto finalTransitions =
        recordRenderGraphTransitions(frame, compiled->finalTransitions, bindings);
    if (!finalTransitions) {
        return std::unexpected{std::move(finalTransitions.error())};
    }

    setBasicRenderViewDiagnostics(view, graph, *compiled, eventRecorder);

    return VulkanFrameRecordResult{
        .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
}

Result<VulkanFrameRecordResult> BasicFullscreenTextureRenderer::recordOffscreenViewportFrame(
    const VulkanFrameRecordContext& frame) {
    return recordOffscreenViewportFrame(frame, frame.extent);
}

Result<VulkanFrameRecordResult>
BasicFullscreenTextureRenderer::recordOffscreenViewportFrame(const VulkanFrameRecordContext& frame,
                                                             VkExtent2D viewportExtent) {
    auto offscreenTarget = ensureOffscreenViewportTarget(frame, frame.format, viewportExtent);
    if (!offscreenTarget) {
        return std::unexpected{std::move(offscreenTarget.error())};
    }
    const VulkanSampledTextureView sampledViewportTarget =
        offscreenViewportTarget_.sampledTextureView();

    BasicRenderViewDesc renderView;
    renderView.target = basicSampledRenderViewTarget(sampledViewportTarget);
    auto view = recordViewFrame(frame, renderView);
    if (!view) {
        return std::unexpected{std::move(view.error())};
    }

    auto pipeline = ensurePipeline(frame.format);
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }
    const VkDescriptorSet compositeDescriptorSet = acquireCompositeDescriptorSet(frame);
    if (compositeDescriptorSet == VK_NULL_HANDLE) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Offscreen viewport composite exhausted per-frame descriptor set ring"}};
    }
    const BasicRenderViewTarget backbufferTarget = basicSwapchainRenderViewTarget(frame);
    const BasicRenderViewTarget viewportTarget =
        basicSampledRenderViewTarget(sampledViewportTarget);

    RenderGraph graph;
    auto backbufferDesc =
        basicRenderViewTargetDesc(backbufferTarget, RenderGraphImageState::Undefined, "Backbuffer");
    if (!backbufferDesc) {
        return std::unexpected{std::move(backbufferDesc.error())};
    }
    auto viewportDesc =
        basicRenderViewTargetDesc(viewportTarget, RenderGraphImageState::ShaderRead,
                                  "OffscreenViewportColor", RenderGraphShaderStage::Fragment);
    if (!viewportDesc) {
        return std::unexpected{std::move(viewportDesc.error())};
    }
    const auto backbuffer = graph.importImage(*backbufferDesc);
    const auto viewport = graph.importImage(*viewportDesc);

    std::vector<VulkanRenderGraphImageBinding> bindings;
    bindings.reserve(2);
    bindings.push_back(basicRenderViewTargetBinding(backbuffer, backbufferTarget));
    bindings.push_back(basicRenderViewTargetBinding(viewport, viewportTarget));

    constexpr BasicFullscreenParams kCompositeParams{
        .tint = {1.0F, 1.0F, 1.0F, 1.0F},
    };

    graph.addPass("CompositeOffscreenViewport", kBasicRasterFullscreenPassType)
        .setParams(kBasicRasterFullscreenParamsType, kCompositeParams)
        .readTexture("source", viewport, RenderGraphShaderStage::Fragment)
        .writeColor("target", backbuffer)
        .recordCommands([kCompositeParams](RenderGraphCommandList& commands) {
            commands.setShader("Hidden/DescriptorLayout", "Fullscreen")
                .setTexture("SourceTex", "source")
                .setVec4("Tint", kCompositeParams.tint)
                .drawFullscreenTriangle();
        })
        .execute([&frame, &bindings, backbufferTarget,
                  compositeDescriptorSet, this](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicFullscreenTexturePass(
                frame, pass, bindings, device_, pipeline_.handle(), pipelineLayout_.handle(),
                compositeDescriptorSet, backbufferTarget.extent,
                BasicFullscreenTexturePassMessages{
                    .paramsContext = "Offscreen viewport composite pass",
                    .unknownTextureSlotMessage =
                        "Offscreen viewport pipeline key references an unknown texture slot",
                },
                nullptr);
        });

    const RenderGraphSchemaRegistry schemas = basicRenderGraphSchemaRegistry();
    auto compiled = graph.compile(schemas);
    if (!compiled) {
        return std::unexpected{std::move(compiled.error())};
    }

    auto executed = graph.execute(*compiled);
    if (!executed) {
        return std::unexpected{std::move(executed.error())};
    }

    auto finalTransitions =
        recordRenderGraphTransitions(frame, compiled->finalTransitions, bindings);
    if (!finalTransitions) {
        return std::unexpected{std::move(finalTransitions.error())};
    }

    return VulkanFrameRecordResult{
        .waitStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
}
