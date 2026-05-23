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
    descriptorSetLayouts_ = std::move(other.descriptorSetLayouts_);
    pipelineLayout_ = std::move(other.pipelineLayout_);
    pipelineCache_ = std::move(other.pipelineCache_);
    pipeline_ = std::move(other.pipeline_);
    pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
    pipelineCacheStats_ = std::exchange(other.pipelineCacheStats_, {});
    offscreenViewportTarget_ = std::move(other.offscreenViewportTarget_);
    descriptorAllocator_ = std::move(other.descriptorAllocator_);
    descriptorSet_ = std::exchange(other.descriptorSet_, VK_NULL_HANDLE);
    compositeDescriptorSet_ = std::exchange(other.compositeDescriptorSet_, VK_NULL_HANDLE);
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

    constexpr std::array poolSizes{
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .count = 2,
        },
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .count = 2,
        },
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .count = 2,
        },
    };
    auto descriptorAllocator = VulkanDescriptorAllocator::create(VulkanDescriptorPoolDesc{
        .device = desc.device,
        .maxSets = 2,
        .poolSizes = poolSizes,
    });
    if (!descriptorAllocator) {
        return std::unexpected{std::move(descriptorAllocator.error())};
    }

    const std::array setLayouts{resources->descriptorSetLayouts.front().handle(),
                                resources->descriptorSetLayouts.front().handle()};
    auto descriptorSets = descriptorAllocator->allocate(VulkanDescriptorSetAllocationDesc{
        .setLayouts = setLayouts,
    });
    if (!descriptorSets) {
        return std::unexpected{std::move(descriptorSets.error())};
    }
    if (descriptorSets->size() != 2 || descriptorSets->front() == VK_NULL_HANDLE ||
        (*descriptorSets)[1] == VK_NULL_HANDLE) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Fullscreen texture renderer failed to allocate two descriptor sets"}};
    }

    const std::array bufferWrites{
        VulkanDescriptorBufferWrite{
            .descriptorSet = descriptorSets->front(),
            .binding = 0,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .buffer = uniformBuffer->handle(),
            .offset = 0,
            .range = uniformBuffer->size(),
        },
        VulkanDescriptorBufferWrite{
            .descriptorSet = (*descriptorSets)[1],
            .binding = 0,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .buffer = uniformBuffer->handle(),
            .offset = 0,
            .range = uniformBuffer->size(),
        },
    };
    updateVulkanDescriptorBuffers(desc.device, bufferWrites);

    const std::array samplerWrites{
        VulkanDescriptorImageWrite{
            .descriptorSet = descriptorSets->front(),
            .binding = 2,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .imageView = VK_NULL_HANDLE,
            .sampler = sampler->handle(),
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        },
        VulkanDescriptorImageWrite{
            .descriptorSet = (*descriptorSets)[1],
            .binding = 2,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .imageView = VK_NULL_HANDLE,
            .sampler = sampler->handle(),
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        },
    };
    updateVulkanDescriptorImages(desc.device, samplerWrites);

    BasicFullscreenTextureRenderer renderer;
    renderer.device_ = desc.device;
    renderer.allocator_ = desc.allocator;
    renderer.vertexShader_ = std::move(*vertexShader);
    renderer.fragmentShader_ = std::move(*fragmentShader);
    renderer.descriptorSetLayouts_ = std::move(resources->descriptorSetLayouts);
    renderer.pipelineLayout_ = std::move(resources->pipelineLayout);
    renderer.pipelineCache_ = std::move(*pipelineCache);
    renderer.descriptorAllocator_ = std::move(*descriptorAllocator);
    renderer.descriptorSet_ = descriptorSets->front();
    renderer.compositeDescriptorSet_ = (*descriptorSets)[1];
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

BasicPipelineCacheStats BasicFullscreenTextureRenderer::pipelineCacheStats() const {
    return pipelineCacheStats_;
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

    constexpr BasicTransferClearParams kClearParams{
        .color = {0.18F, 0.36F, 0.95F, 1.0F},
    };
    constexpr BasicFullscreenParams kFullscreenParams{
        .tint = {1.0F, 1.0F, 1.0F, 1.0F},
    };

    graph.addPass("ClearFullscreenSource", kBasicTransferClearPassType)
        .setParams(kBasicTransferClearParamsType, kClearParams)
        .writeTransfer("target", source)
        .recordCommands([kClearParams](RenderGraphCommandList& commands) {
            commands.clearColor("target", kClearParams.color);
        })
        .execute([&frame, &bindings, &eventRecorder](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicFullscreenSourceClear(frame, pass, bindings, &eventRecorder);
        });

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
        .execute([&frame, &bindings, viewTarget, &eventRecorder,
                  this](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicFullscreenTexturePass(
                frame, pass, bindings, device_, pipeline_.handle(), pipelineLayout_.handle(),
                descriptorSet_, viewTarget.extent,
                BasicFullscreenTexturePassMessages{
                    .paramsContext = "Fullscreen texture pass",
                    .unknownTextureSlotMessage =
                        "Fullscreen pipeline key references an unknown texture slot",
                },
                &eventRecorder);
        });

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
    auto debugPreviewAdded = tryAddBasicDebugPreviewPass(
        graph, view.debugPreview, debugPreviewCandidates, bindings, frame, &eventRecorder);
    if (!debugPreviewAdded) {
        return std::unexpected{std::move(debugPreviewAdded.error())};
    }

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
                  this](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicFullscreenTexturePass(
                frame, pass, bindings, device_, pipeline_.handle(), pipelineLayout_.handle(),
                compositeDescriptorSet_, backbufferTarget.extent,
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
