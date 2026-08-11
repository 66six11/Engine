BasicFullscreenTextureRenderer::BasicFullscreenTextureRenderer(
    BasicFullscreenTextureRenderer&& other) noexcept {
    *this = std::move(other);
}

namespace {

    constexpr std::size_t kDescriptorSetRingSize = 16U;
    constexpr std::size_t kDebugLineVertexBufferRingSize = 16U;
    constexpr std::size_t kFrameResourceContextCount = 4U;
    constexpr std::size_t kFullscreenPipelineCacheCapacity = 2U;
    constexpr std::size_t kOverlayPipelineCacheCapacity = 4U;
    constexpr std::size_t kSceneMeshPipelineCacheCapacity = 4U;
    constexpr std::size_t kResourcesPerFrameContext =
        kDescriptorSetRingSize / kFrameResourceContextCount;

    static_assert(kDescriptorSetRingSize % kFrameResourceContextCount == 0U);
    static_assert(kDebugLineVertexBufferRingSize == kDescriptorSetRingSize);

} // namespace

BasicRenderFrameResourceContext::BasicRenderFrameResourceContext(std::size_t index) noexcept
    : index_{index} {}

std::size_t BasicRenderFrameResourceContext::index() const noexcept {
    return index_;
}

void BasicRenderFrameResourceContext::beginFrame() noexcept {
    fullscreenDescriptorCursor_ = 0U;
    compositeDescriptorCursor_ = 0U;
    debugLineVertexBufferCursor_ = 0U;
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
    sceneMeshVertexShader_ = std::move(other.sceneMeshVertexShader_);
    sceneMeshFragmentShader_ = std::move(other.sceneMeshFragmentShader_);
    descriptorSetLayouts_ = std::move(other.descriptorSetLayouts_);
    pipelineLayout_ = std::move(other.pipelineLayout_);
    worldGridPipelineLayout_ = std::move(other.worldGridPipelineLayout_);
    debugLinePipelineLayout_ = std::move(other.debugLinePipelineLayout_);
    sceneMeshPipelineLayout_ = std::move(other.sceneMeshPipelineLayout_);
    pipelineCache_ = std::move(other.pipelineCache_);
    fullscreenPipelines_ = std::move(other.fullscreenPipelines_);
    worldGridPipelines_ = std::move(other.worldGridPipelines_);
    debugLinePipelines_ = std::move(other.debugLinePipelines_);
    sceneMeshPipelines_ = std::move(other.sceneMeshPipelines_);
    pipelineCacheStats_ = std::exchange(other.pipelineCacheStats_, {});
    worldGridPipelineCacheStats_ = std::exchange(other.worldGridPipelineCacheStats_, {});
    debugLinePipelineCacheStats_ = std::exchange(other.debugLinePipelineCacheStats_, {});
    sceneMeshPipelineCacheStats_ = std::exchange(other.sceneMeshPipelineCacheStats_, {});
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
    sceneMeshVertexBuffer_ = std::move(other.sceneMeshVertexBuffer_);
    sceneMeshIndexBuffer_ = std::move(other.sceneMeshIndexBuffer_);
    uniformBuffer_ = std::move(other.uniformBuffer_);
    sampler_ = std::move(other.sampler_);
    transientImagePool_ = std::move(other.transientImagePool_);
    transientImages_ = std::move(other.transientImages_);
    deviceCapabilities_ = std::exchange(other.deviceCapabilities_, {});
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
    auto sceneMeshReflection = validateMesh3DReflection(desc.shaderDirectory);
    if (!sceneMeshReflection) {
        return std::unexpected{std::move(sceneMeshReflection.error())};
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
    auto sceneMeshVertexCode = readSpirvFile(desc.shaderDirectory / "basic_mesh3d.vert.spv");
    if (!sceneMeshVertexCode) {
        return std::unexpected{std::move(sceneMeshVertexCode.error())};
    }
    auto sceneMeshFragmentCode = readSpirvFile(desc.shaderDirectory / "basic_mesh3d.frag.spv");
    if (!sceneMeshFragmentCode) {
        return std::unexpected{std::move(sceneMeshFragmentCode.error())};
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
    auto sceneMeshVertexShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *sceneMeshVertexCode,
    });
    if (!sceneMeshVertexShader) {
        return std::unexpected{std::move(sceneMeshVertexShader.error())};
    }
    auto sceneMeshFragmentShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *sceneMeshFragmentCode,
    });
    if (!sceneMeshFragmentShader) {
        return std::unexpected{std::move(sceneMeshFragmentShader.error())};
    }
    constexpr std::array worldGridPushConstantRanges{
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = static_cast<std::uint32_t>(sizeof(BasicRenderViewWorldGridPushConstants)),
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
    constexpr std::array sceneMeshPushConstantRanges{
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = static_cast<std::uint32_t>(sizeof(BasicMesh3DPushConstants)),
        },
    };
    auto sceneMeshPipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
        .device = desc.device,
        .setLayouts = {},
        .pushConstantRanges = sceneMeshPushConstantRanges,
    });
    if (!sceneMeshPipelineLayout) {
        return std::unexpected{std::move(sceneMeshPipelineLayout.error())};
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
    renderer.sceneMeshVertexShader_ = std::move(*sceneMeshVertexShader);
    renderer.sceneMeshFragmentShader_ = std::move(*sceneMeshFragmentShader);
    renderer.descriptorSetLayouts_ = std::move(resources->descriptorSetLayouts);
    renderer.pipelineLayout_ = std::move(resources->pipelineLayout);
    renderer.worldGridPipelineLayout_ = std::move(*worldGridPipelineLayout);
    renderer.debugLinePipelineLayout_ = std::move(*debugLinePipelineLayout);
    renderer.sceneMeshPipelineLayout_ = std::move(*sceneMeshPipelineLayout);
    renderer.pipelineCache_ = std::move(*pipelineCache);
    renderer.descriptorAllocator_ = std::move(*descriptorAllocator);
    renderer.descriptorSets_ = std::move(fullscreenDescriptorSets);
    renderer.compositeDescriptorSets_ = std::move(compositeDescriptorSets);
    renderer.debugLineVertexBuffers_.resize(kDebugLineVertexBufferRingSize);
    renderer.debugLineVertexBufferSizes_.resize(kDebugLineVertexBufferRingSize);
    renderer.uniformBuffer_ = std::move(*uniformBuffer);
    renderer.sampler_ = std::move(*sampler);
    renderer.deviceCapabilities_ = desc.deviceCapabilities;
    return renderer;
}

Result<VkPipeline> BasicFullscreenTextureRenderer::ensurePipeline(VkFormat colorFormat) {
    const auto cached = std::ranges::find_if(
        fullscreenPipelines_, [colorFormat](const FullscreenPipelineCacheEntry& entry) {
            return entry.colorFormat == colorFormat;
        });
    if (cached != fullscreenPipelines_.end()) {
        ++pipelineCacheStats_.reused;
        return cached->pipeline.handle();
    }
    if (fullscreenPipelines_.size() >= kFullscreenPipelineCacheCapacity) {
        return std::unexpected{Error{
            ErrorDomain::Vulkan,
            0,
            "Fullscreen texture pipeline cache exhausted its supported format key space",
        }};
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
        .deviceCapabilities = {},
    });
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }
    fullscreenPipelines_.push_back(FullscreenPipelineCacheEntry{
        .colorFormat = colorFormat,
        .pipeline = std::move(*pipeline),
    });
    ++pipelineCacheStats_.created;
    return fullscreenPipelines_.back().pipeline.handle();
}

Result<VkPipeline>
BasicFullscreenTextureRenderer::ensureWorldGridPipeline(VkFormat colorFormat,
                                                        BasicRenderViewOverlayBlendMode blendMode) {
    const auto cached = std::ranges::find_if(
        worldGridPipelines_, [colorFormat, blendMode](const OverlayPipelineCacheEntry& entry) {
            return entry.colorFormat == colorFormat && entry.blendMode == blendMode;
        });
    if (cached != worldGridPipelines_.end()) {
        ++worldGridPipelineCacheStats_.reused;
        return cached->pipeline.handle();
    }
    if (worldGridPipelines_.size() >= kOverlayPipelineCacheCapacity) {
        return std::unexpected{Error{
            ErrorDomain::Vulkan,
            0,
            "RenderView world-grid pipeline cache exhausted its supported format/blend key "
            "space",
        }};
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
        .deviceCapabilities = {},
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

    worldGridPipelines_.push_back(OverlayPipelineCacheEntry{
        .colorFormat = colorFormat,
        .blendMode = blendMode,
        .pipeline = std::move(*pipeline),
    });
    ++worldGridPipelineCacheStats_.created;
    return worldGridPipelines_.back().pipeline.handle();
}

Result<VkPipeline>
BasicFullscreenTextureRenderer::ensureDebugLinePipeline(VkFormat colorFormat,
                                                        BasicRenderViewOverlayBlendMode blendMode) {
    const auto cached = std::ranges::find_if(
        debugLinePipelines_, [colorFormat, blendMode](const OverlayPipelineCacheEntry& entry) {
            return entry.colorFormat == colorFormat && entry.blendMode == blendMode;
        });
    if (cached != debugLinePipelines_.end()) {
        ++debugLinePipelineCacheStats_.reused;
        return cached->pipeline.handle();
    }
    if (debugLinePipelines_.size() >= kOverlayPipelineCacheCapacity) {
        return std::unexpected{Error{
            ErrorDomain::Vulkan,
            0,
            "RenderView debug-line pipeline cache exhausted its supported format/blend key space",
        }};
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
        .deviceCapabilities = {},
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

    debugLinePipelines_.push_back(OverlayPipelineCacheEntry{
        .colorFormat = colorFormat,
        .blendMode = blendMode,
        .pipeline = std::move(*pipeline),
    });
    ++debugLinePipelineCacheStats_.created;
    return debugLinePipelines_.back().pipeline.handle();
}

Result<void> BasicFullscreenTextureRenderer::ensureSceneMeshResources() {
    if (sceneMeshVertexBuffer_.handle() != VK_NULL_HANDLE &&
        sceneMeshIndexBuffer_.handle() != VK_NULL_HANDLE) {
        return {};
    }

    constexpr auto validationMesh = validation::directionalWedgeValidationMeshProduct();
    std::vector<BasicVertex3D> validationVertices;
    validationVertices.reserve(validationMesh.vertices.size());
    for (const validation::ValidationMeshVertex& vertex : validationMesh.vertices) {
        validationVertices.push_back(BasicVertex3D{
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .color = {vertex.color[0], vertex.color[1], vertex.color[2]},
        });
    }
    const std::span<const BasicVertex3D> normalizedVertices{validationVertices};
    auto vertexBuffer = VulkanBuffer::create(VulkanBufferDesc{
        .device = device_,
        .allocator = allocator_,
        .size = normalizedVertices.size_bytes(),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
    });
    if (!vertexBuffer) {
        return std::unexpected{std::move(vertexBuffer.error())};
    }
    auto uploadedVertices = vertexBuffer->upload(std::as_bytes(normalizedVertices));
    if (!uploadedVertices) {
        return std::unexpected{std::move(uploadedVertices.error())};
    }
    auto indexBuffer = VulkanBuffer::create(VulkanBufferDesc{
        .device = device_,
        .allocator = allocator_,
        .size = validationMesh.indices.size_bytes(),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
    });
    if (!indexBuffer) {
        return std::unexpected{std::move(indexBuffer.error())};
    }
    auto uploadedIndices = indexBuffer->upload(std::as_bytes(validationMesh.indices));
    if (!uploadedIndices) {
        return std::unexpected{std::move(uploadedIndices.error())};
    }

    sceneMeshVertexBuffer_ = std::move(*vertexBuffer);
    sceneMeshIndexBuffer_ = std::move(*indexBuffer);
    return {};
}

Result<VkPipeline>
BasicFullscreenTextureRenderer::ensureSceneMeshPipeline(VkFormat colorFormat, VkFormat depthFormat,
                                                        BasicSceneRasterMode rasterMode) {
    VkPolygonMode polygonMode{VK_POLYGON_MODE_FILL};
    switch (rasterMode) {
    case BasicSceneRasterMode::Solid:
        break;
    case BasicSceneRasterMode::Wireframe:
        polygonMode = VK_POLYGON_MODE_LINE;
        break;
    default:
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0, "RenderView scene raster mode is invalid"}};
    }

    const auto cached = std::ranges::find_if(
        sceneMeshPipelines_,
        [colorFormat, depthFormat, rasterMode](const SceneMeshPipelineCacheEntry& entry) {
            return entry.colorFormat == colorFormat && entry.depthFormat == depthFormat &&
                   entry.rasterMode == rasterMode;
        });
    if (cached != sceneMeshPipelines_.end()) {
        ++sceneMeshPipelineCacheStats_.reused;
        return cached->pipeline.handle();
    }
    if (sceneMeshPipelines_.size() >= kSceneMeshPipelineCacheCapacity) {
        return std::unexpected{Error{
            ErrorDomain::Vulkan,
            0,
            "RenderView scene mesh pipeline cache exhausted its supported format/raster key "
            "space",
        }};
    }

    const auto bindings = basicVertex3DInputBindings();
    const auto attributes = basicVertex3DInputAttributes();
    auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
        .device = device_,
        .pipelineCache = pipelineCache_.handle(),
        .layout = sceneMeshPipelineLayout_.handle(),
        .vertexShader = sceneMeshVertexShader_.handle(),
        .fragmentShader = sceneMeshFragmentShader_.handle(),
        .vertexEntryPoint = "main",
        .fragmentEntryPoint = "main",
        .colorFormat = colorFormat,
        .depthFormat = depthFormat,
        .vertexBindings = bindings,
        .vertexAttributes = attributes,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .polygonMode = polygonMode,
        .deviceCapabilities = deviceCapabilities_,
    });
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }

    sceneMeshPipelines_.push_back(SceneMeshPipelineCacheEntry{
        .colorFormat = colorFormat,
        .depthFormat = depthFormat,
        .rasterMode = rasterMode,
        .pipeline = std::move(*pipeline),
    });
    ++sceneMeshPipelineCacheStats_.created;
    return sceneMeshPipelines_.back().pipeline.handle();
}

BasicPipelineCacheStats BasicFullscreenTextureRenderer::pipelineCacheStats() const {
    return pipelineCacheStats_;
}

BasicPipelineCacheStats BasicFullscreenTextureRenderer::worldGridPipelineCacheStats() const {
    return worldGridPipelineCacheStats_;
}

BasicPipelineCacheStats BasicFullscreenTextureRenderer::debugLinePipelineCacheStats() const {
    return debugLinePipelineCacheStats_;
}

BasicPipelineCacheStats BasicFullscreenTextureRenderer::sceneMeshPipelineCacheStats() const {
    return sceneMeshPipelineCacheStats_;
}

VkDescriptorSet BasicFullscreenTextureRenderer::acquireFullscreenDescriptorSet(
    const VulkanFrameRecordContext& frame, BasicRenderFrameResourceContext* frameResources) {
    if (frameResources != nullptr) {
        if (frameResources->fullscreenDescriptorCursor_ >= kResourcesPerFrameContext) {
            return VK_NULL_HANDLE;
        }
        const std::size_t resourceIndex = frameResources->index_ * kResourcesPerFrameContext +
                                          frameResources->fullscreenDescriptorCursor_++;
        return descriptorSets_[resourceIndex];
    }

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
    const VulkanFrameRecordContext& frame, BasicRenderFrameResourceContext* frameResources) {
    if (frameResources != nullptr) {
        if (frameResources->compositeDescriptorCursor_ >= kResourcesPerFrameContext) {
            return VK_NULL_HANDLE;
        }
        const std::size_t resourceIndex = frameResources->index_ * kResourcesPerFrameContext +
                                          frameResources->compositeDescriptorCursor_++;
        return compositeDescriptorSets_[resourceIndex];
    }

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

void BasicFullscreenTextureRenderer::resetFrameResourceCursors() noexcept {
    descriptorSetEpoch_ = 0U;
    compositeDescriptorSetEpoch_ = 0U;
    debugLineVertexBufferEpoch_ = 0U;
    descriptorSetCursor_ = 0U;
    compositeDescriptorSetCursor_ = 0U;
    debugLineVertexBufferCursor_ = 0U;
}

Result<BasicRenderFrameResourceContext>
BasicFullscreenTextureRenderer::createFrameResourceContext(std::size_t index) const {
    if (index >= kFrameResourceContextCount || descriptorSets_.size() < kDescriptorSetRingSize ||
        compositeDescriptorSets_.size() < kDescriptorSetRingSize ||
        debugLineVertexBuffers_.size() < kDebugLineVertexBufferRingSize) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Fullscreen texture renderer frame resource context is unavailable"}};
    }

    return BasicRenderFrameResourceContext{index};
}

Result<VkBuffer> BasicFullscreenTextureRenderer::uploadDebugLineVertices(
    const VulkanFrameRecordContext& frame, std::span<const std::byte> vertices,
    BasicRenderFrameResourceContext* frameResources) {
    if (vertices.empty()) {
        return VK_NULL_HANDLE;
    }

    std::size_t resourceIndex{};
    if (frameResources != nullptr) {
        if (frameResources->debugLineVertexBufferCursor_ >= kResourcesPerFrameContext) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0,
                      "Fullscreen texture renderer exhausted frame-context debug line "
                      "vertex buffers"}};
        }
        resourceIndex = frameResources->index_ * kResourcesPerFrameContext +
                        frameResources->debugLineVertexBufferCursor_++;
    } else {
        const std::uint64_t epoch =
            frame.frameLoop == nullptr ? 0U : frame.frameLoop->submittedFrameEpoch() + 1U;
        if (debugLineVertexBufferEpoch_ != epoch) {
            debugLineVertexBufferEpoch_ = epoch;
            debugLineVertexBufferCursor_ = 0;
        }
        if (debugLineVertexBufferCursor_ >= debugLineVertexBuffers_.size()) {
            return std::unexpected{Error{
                ErrorDomain::Vulkan, 0,
                "Fullscreen texture renderer exhausted per-frame debug line vertex buffer ring"}};
        }
        resourceIndex = debugLineVertexBufferCursor_++;
    }

    VulkanBuffer& buffer = debugLineVertexBuffers_[resourceIndex];
    VkDeviceSize& bufferSize = debugLineVertexBufferSizes_[resourceIndex];

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
    accumulateBufferStats(stats, sceneMeshVertexBuffer_);
    accumulateBufferStats(stats, sceneMeshIndexBuffer_);
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
    return recordViewFrame(frame, std::move(view), nullptr, transientImagePool_, transientImages_);
}

Result<VulkanFrameRecordResult> BasicFullscreenTextureRenderer::recordViewFrame(
    const VulkanFrameRecordContext& frame, BasicRenderViewDesc view,
    VulkanTransientImagePool& transientImagePool,
    std::vector<VulkanTransientImageResource>& transientImages) {
    return recordViewFrame(frame, std::move(view), nullptr, transientImagePool, transientImages);
}

Result<VulkanFrameRecordResult> BasicFullscreenTextureRenderer::recordViewFrame(
    const VulkanFrameRecordContext& frame, BasicRenderViewDesc view,
    BasicRenderFrameResourceContext& frameResources, VulkanTransientImagePool& transientImagePool,
    std::vector<VulkanTransientImageResource>& transientImages) {
    frameResources.beginFrame();
    return recordViewFrame(frame, std::move(view), &frameResources, transientImagePool,
                           transientImages);
}

Result<VulkanFrameRecordResult> BasicFullscreenTextureRenderer::recordViewFrame(
    const VulkanFrameRecordContext& frame, BasicRenderViewDesc view,
    BasicRenderFrameResourceContext* frameResources, VulkanTransientImagePool& transientImagePool,
    std::vector<VulkanTransientImageResource>& transientImages) {
    auto target = validateBasicRenderViewTarget(view.target, "Fullscreen render view");
    if (!target) {
        return std::unexpected{std::move(target.error())};
    }
    auto targetFormat =
        basicRenderGraphImageFormat(view.target.format, "Fullscreen render view target");
    if (!targetFormat) {
        return std::unexpected{std::move(targetFormat.error())};
    }

    std::vector<BasicDrawListItem> sceneDrawItems(view.scene.drawItems.begin(),
                                                  view.scene.drawItems.end());
    view.scene.drawItems = std::span<const BasicDrawListItem>{sceneDrawItems};
    std::vector<BasicDebugWorldLine> debugWorldLines(view.overlay.debugWorldLines.begin(),
                                                     view.overlay.debugWorldLines.end());
    view.overlay.debugWorldLines = std::span<const BasicDebugWorldLine>{debugWorldLines};
    auto sceneMeshValidated = validateBasicRenderViewSceneMesh(view);
    if (!sceneMeshValidated) {
        return std::unexpected{std::move(sceneMeshValidated.error())};
    }
    auto renderViewPassPolicyResult = basicRenderViewPassPolicy(view, debugWorldLines);
    if (!renderViewPassPolicyResult) {
        return std::unexpected{std::move(renderViewPassPolicyResult.error())};
    }
    const BasicRenderViewPassPolicy renderViewPassPolicy = *renderViewPassPolicyResult;

    auto pipeline = ensurePipeline(view.target.format);
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }
    const VkPipeline fullscreenPipeline = *pipeline;
    constexpr VkFormat kSceneDepthFormat = VK_FORMAT_D32_SFLOAT;
    VkPipeline sceneMeshPipeline = VK_NULL_HANDLE;
    if (renderViewPassPolicy.sceneMeshEnabled) {
        if (view.scene.rasterMode == BasicSceneRasterMode::Wireframe &&
            !deviceCapabilities_.fillModeNonSolid) {
            setBasicRenderViewWireframeUnavailableDiagnostics(view);
            return std::unexpected{Error{
                ErrorDomain::Vulkan,
                static_cast<int>(VK_ERROR_FEATURE_NOT_PRESENT),
                "RenderView scene wireframe requires the logical-device fillModeNonSolid "
                "capability; no solid fallback was selected",
            }};
        }
        auto resources = ensureSceneMeshResources();
        if (!resources) {
            return std::unexpected{std::move(resources.error())};
        }
        auto scenePipeline =
            ensureSceneMeshPipeline(view.target.format, kSceneDepthFormat, view.scene.rasterMode);
        if (!scenePipeline) {
            return std::unexpected{std::move(scenePipeline.error())};
        }
        sceneMeshPipeline = *scenePipeline;
    }
    const VkDescriptorSet fullscreenDescriptorSet =
        acquireFullscreenDescriptorSet(frame, frameResources);
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
    RenderGraphImageHandle sceneDepth{};
    if (renderViewPassPolicy.sceneMeshEnabled) {
        sceneDepth = graph.createTransientImage(RenderGraphImageDesc{
            .name = "RenderViewSceneDepth",
            .format = RenderGraphImageFormat::D32Sfloat,
            .extent = basicRenderGraphExtent(viewTarget.extent),
        });
    }

    RenderGraphBufferHandle sceneVertices{};
    RenderGraphBufferHandle sceneIndices{};
    std::vector<VulkanRenderGraphBufferBinding> bufferBindings;
    if (renderViewPassPolicy.sceneMeshEnabled) {
        sceneVertices = graph.importBuffer(RenderGraphBufferDesc{
            .name = "RenderViewValidationMeshVertices",
            .byteSize = sceneMeshVertexBuffer_.size(),
            .initialState = RenderGraphBufferState::VertexRead,
            .initialShaderStage = RenderGraphShaderStage::None,
            .finalState = RenderGraphBufferState::VertexRead,
            .finalShaderStage = RenderGraphShaderStage::None,
        });
        sceneIndices = graph.importBuffer(RenderGraphBufferDesc{
            .name = "RenderViewValidationMeshIndices",
            .byteSize = sceneMeshIndexBuffer_.size(),
            .initialState = RenderGraphBufferState::IndexRead,
            .initialShaderStage = RenderGraphShaderStage::None,
            .finalState = RenderGraphBufferState::IndexRead,
            .finalShaderStage = RenderGraphShaderStage::None,
        });

        bufferBindings.reserve(2);
        bufferBindings.push_back(VulkanRenderGraphBufferBinding{
            .buffer = sceneVertices,
            .vulkanBuffer = sceneMeshVertexBuffer_.handle(),
            .offset = 0,
            .size = sceneMeshVertexBuffer_.size(),
            .debugName = "RenderViewValidationMeshVertices",
        });
        auto namedVertices = setVulkanRenderGraphBufferDebugName(frame, bufferBindings.back());
        if (!namedVertices) {
            return std::unexpected{std::move(namedVertices.error())};
        }
        bufferBindings.push_back(VulkanRenderGraphBufferBinding{
            .buffer = sceneIndices,
            .vulkanBuffer = sceneMeshIndexBuffer_.handle(),
            .offset = 0,
            .size = sceneMeshIndexBuffer_.size(),
            .debugName = "RenderViewValidationMeshIndices",
        });
        auto namedIndices = setVulkanRenderGraphBufferDebugName(frame, bufferBindings.back());
        if (!namedIndices) {
            return std::unexpected{std::move(namedIndices.error())};
        }
    }

    std::vector<VulkanRenderGraphImageBinding> bindings;
    bindings.reserve(4);
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

    BasicRenderViewPassRecordingContext renderViewRecording{
        .graph = graph,
        .renderTarget = renderTarget,
        .sceneDepth = sceneDepth,
        .sceneVertices = sceneVertices,
        .sceneIndices = sceneIndices,
        .policy = renderViewPassPolicy,
        .frame = frame,
        .bindings = bindings,
        .bufferBindings = bufferBindings,
        .viewTarget = viewTarget,
        .camera = view.camera,
        .sceneDrawItems = sceneDrawItems,
        .sceneRasterMode = view.scene.rasterMode,
        .colorLoadOp = view.overlay.colorLoadOp,
        .colorStoreOp = view.overlay.colorStoreOp,
        .eventRecorder = eventRecorder,
    };
    VkPipeline worldGridPipeline = VK_NULL_HANDLE;
    if (renderViewPassPolicy.worldGridEnabled) {
        auto ensuredWorldGridPipeline =
            ensureWorldGridPipeline(view.target.format, view.overlay.blendMode);
        if (!ensuredWorldGridPipeline) {
            return std::unexpected{std::move(ensuredWorldGridPipeline.error())};
        }
        worldGridPipeline = *ensuredWorldGridPipeline;
    }
    std::vector<BasicDebugLineVertex> debugLineVertices;
    VkBuffer debugLineVertexBuffer = VK_NULL_HANDLE;
    std::uint32_t debugLineVertexCount = 0;
    VkPipeline debugLinePipeline = VK_NULL_HANDLE;
    if (renderViewPassPolicy.debugLineOverlayEnabled) {
        auto ensuredDebugLinePipeline =
            ensureDebugLinePipeline(view.target.format, view.overlay.blendMode);
        if (!ensuredDebugLinePipeline) {
            return std::unexpected{std::move(ensuredDebugLinePipeline.error())};
        }
        debugLinePipeline = *ensuredDebugLinePipeline;
        debugLineVertices = basicDebugLineVertices(view.camera, debugWorldLines);
        if (!debugLineVertices.empty()) {
            if (debugLineVertices.size() > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected{
                    Error{ErrorDomain::Vulkan, 0,
                          "RenderView debug line vertex count exceeds Vulkan draw limits"}};
            }
            debugLineVertexCount = static_cast<std::uint32_t>(debugLineVertices.size());
            auto uploadedDebugLines = uploadDebugLineVertices(
                frame, std::as_bytes(std::span{debugLineVertices}), frameResources);
            if (!uploadedDebugLines) {
                return std::unexpected{std::move(uploadedDebugLines.error())};
            }
            debugLineVertexBuffer = *uploadedDebugLines;
        }
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
                  fullscreenPipeline, this](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicFullscreenTexturePass(
                frame, pass, bindings, device_, fullscreenPipeline, pipelineLayout_.handle(),
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
        addBasicRenderViewWorldGridPass(renderViewRecording, worldGridPipeline,
                                        worldGridPipelineLayout_.handle());
        auto debugPreviewAfterWorldGrid = debugPreviewCursor.tryAddPreviewAfterSourcePass();
        if (!debugPreviewAfterWorldGrid) {
            return std::unexpected{std::move(debugPreviewAfterWorldGrid.error())};
        }
    }

    if (renderViewPassPolicy.sceneMeshEnabled) {
        addBasicRenderViewSceneMeshPass(renderViewRecording, sceneMeshPipeline,
                                        sceneMeshPipelineLayout_.handle());
        auto debugPreviewAfterSceneMesh = debugPreviewCursor.tryAddPreviewAfterSourcePass();
        if (!debugPreviewAfterSceneMesh) {
            return std::unexpected{std::move(debugPreviewAfterSceneMesh.error())};
        }
    }

    if (renderViewPassPolicy.debugLineOverlayEnabled) {
        addBasicRenderViewOverlayPass(renderViewRecording, debugLinePipeline, debugLineVertexBuffer,
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
                                              transientImagePool, transientImages);
    if (!prepared) {
        return std::unexpected{std::move(prepared.error())};
    }

    auto executed = graph.execute(*compiled);
    if (!executed) {
        return std::unexpected{std::move(executed.error())};
    }

    auto finalBufferTransitions =
        recordRenderGraphBufferTransitions(frame, compiled->finalBufferTransitions, bufferBindings);
    if (!finalBufferTransitions) {
        return std::unexpected{std::move(finalBufferTransitions.error())};
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
    const VkPipeline fullscreenPipeline = *pipeline;
    const VkDescriptorSet compositeDescriptorSet = acquireCompositeDescriptorSet(frame, nullptr);
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
        .execute([&frame, &bindings, backbufferTarget, compositeDescriptorSet, fullscreenPipeline,
                  this](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicFullscreenTexturePass(
                frame, pass, bindings, device_, fullscreenPipeline, pipelineLayout_.handle(),
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
