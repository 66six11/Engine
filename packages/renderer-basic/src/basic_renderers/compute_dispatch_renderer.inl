BasicComputeDispatchRenderer::BasicComputeDispatchRenderer(
    BasicComputeDispatchRenderer&& other) noexcept {
    *this = std::move(other);
}

BasicComputeDispatchRenderer&
BasicComputeDispatchRenderer::operator=(BasicComputeDispatchRenderer&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    allocator_ = std::exchange(other.allocator_, nullptr);
    computeShader_ = std::move(other.computeShader_);
    descriptorSetLayouts_ = std::move(other.descriptorSetLayouts_);
    pipelineLayout_ = std::move(other.pipelineLayout_);
    pipelineCache_ = std::move(other.pipelineCache_);
    pipeline_ = std::move(other.pipeline_);
    pipelineCacheStats_ = std::exchange(other.pipelineCacheStats_, {});
    descriptorAllocator_ = std::move(other.descriptorAllocator_);
    descriptorSet_ = std::exchange(other.descriptorSet_, VK_NULL_HANDLE);
    storageBuffer_ = std::move(other.storageBuffer_);
    readbackBuffer_ = std::move(other.readbackBuffer_);
    computeStats_ = std::exchange(other.computeStats_, {});
    return *this;
}

Result<BasicComputeDispatchRenderer>
BasicComputeDispatchRenderer::create(const BasicComputeDispatchRendererDesc& desc) {
    if (desc.device == VK_NULL_HANDLE) {
        return std::unexpected{Error{ErrorDomain::Vulkan, 0,
                                     "Cannot create compute dispatch renderer without a device"}};
    }
    if (desc.allocator == nullptr) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Cannot create compute dispatch renderer without an allocator"}};
    }
    if (!desc.graphicsQueueSupportsCompute) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Compute dispatch renderer requires a graphics queue with compute support"}};
    }

    auto signature = validateBasicComputeReflection(desc.shaderDirectory);
    if (!signature) {
        return std::unexpected{std::move(signature.error())};
    }
    auto resources = createPipelineLayoutResources(desc.device, *signature);
    if (!resources) {
        return std::unexpected{std::move(resources.error())};
    }
    if (resources->descriptorSetLayouts.empty()) {
        return std::unexpected{Error{
            ErrorDomain::Vulkan, 0, "Compute dispatch renderer produced no descriptor set layout"}};
    }

    auto computeCode = readSpirvFile(desc.shaderDirectory / "basic_compute.comp.spv");
    if (!computeCode) {
        return std::unexpected{std::move(computeCode.error())};
    }
    auto computeShader = VulkanShaderModule::create(VulkanShaderModuleDesc{
        .device = desc.device,
        .code = *computeCode,
    });
    if (!computeShader) {
        return std::unexpected{std::move(computeShader.error())};
    }

    const auto byteSize =
        static_cast<VkDeviceSize>(sizeof(std::uint32_t) * kBasicComputeValueCount);
    auto storageBuffer = VulkanBuffer::create(VulkanBufferDesc{
        .device = desc.device,
        .allocator = desc.allocator,
        .size = byteSize,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .memoryUsage = VulkanBufferMemoryUsage::DeviceLocal,
    });
    if (!storageBuffer) {
        return std::unexpected{std::move(storageBuffer.error())};
    }
    auto readbackBuffer = VulkanBuffer::create(VulkanBufferDesc{
        .device = desc.device,
        .allocator = desc.allocator,
        .size = byteSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .memoryUsage = VulkanBufferMemoryUsage::HostReadback,
    });
    if (!readbackBuffer) {
        return std::unexpected{std::move(readbackBuffer.error())};
    }

    constexpr std::array poolSizes{
        VulkanDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .count = 1,
        },
    };
    auto descriptorAllocator = VulkanDescriptorAllocator::create(VulkanDescriptorPoolDesc{
        .device = desc.device,
        .maxSets = 1,
        .poolSizes = poolSizes,
    });
    if (!descriptorAllocator) {
        return std::unexpected{std::move(descriptorAllocator.error())};
    }

    const std::array setLayouts{resources->descriptorSetLayouts.front().handle()};
    auto descriptorSets = descriptorAllocator->allocate(VulkanDescriptorSetAllocationDesc{
        .setLayouts = setLayouts,
    });
    if (!descriptorSets) {
        return std::unexpected{std::move(descriptorSets.error())};
    }
    if (descriptorSets->size() != 1 || descriptorSets->front() == VK_NULL_HANDLE) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Compute dispatch renderer failed to allocate one descriptor set"}};
    }

    const std::array descriptorWrites{
        VulkanDescriptorBufferWrite{
            .descriptorSet = descriptorSets->front(),
            .binding = 0,
            .arrayElement = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .buffer = storageBuffer->handle(),
            .offset = 0,
            .range = storageBuffer->size(),
        },
    };
    updateVulkanDescriptorBuffers(desc.device, descriptorWrites);

    auto pipelineCache = createBasicPipelineCache(desc.device);
    if (!pipelineCache) {
        return std::unexpected{std::move(pipelineCache.error())};
    }
    auto pipeline = VulkanComputePipeline::create(VulkanComputePipelineDesc{
        .device = desc.device,
        .pipelineCache = pipelineCache->handle(),
        .layout = resources->pipelineLayout.handle(),
        .shader = computeShader->handle(),
        .entryPoint = "main",
    });
    if (!pipeline) {
        return std::unexpected{std::move(pipeline.error())};
    }

    BasicComputeDispatchRenderer renderer;
    renderer.device_ = desc.device;
    renderer.allocator_ = desc.allocator;
    renderer.computeShader_ = std::move(*computeShader);
    renderer.descriptorSetLayouts_ = std::move(resources->descriptorSetLayouts);
    renderer.pipelineLayout_ = std::move(resources->pipelineLayout);
    renderer.pipelineCache_ = std::move(*pipelineCache);
    renderer.pipeline_ = std::move(*pipeline);
    renderer.pipelineCacheStats_.created = 1;
    renderer.descriptorAllocator_ = std::move(*descriptorAllocator);
    renderer.descriptorSet_ = descriptorSets->front();
    renderer.storageBuffer_ = std::move(*storageBuffer);
    renderer.readbackBuffer_ = std::move(*readbackBuffer);
    return renderer;
}

Result<VulkanFrameRecordResult>
BasicComputeDispatchRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
    if (pipeline_.handle() == VK_NULL_HANDLE || descriptorSet_ == VK_NULL_HANDLE) {
        return std::unexpected{
            Error{ErrorDomain::Vulkan, 0,
                  "Cannot record compute dispatch without initialized resources"}};
    }

    const VkDeviceSize byteSize = storageBuffer_.size();

    RenderGraph graph;
    auto backbufferDesc = basicBackbufferDesc(frame);
    if (!backbufferDesc) {
        return std::unexpected{std::move(backbufferDesc.error())};
    }
    const auto backbuffer = graph.importImage(*backbufferDesc);
    const auto storage = graph.importBuffer(RenderGraphBufferDesc{
        .name = "ComputeStorageBuffer",
        .byteSize = byteSize,
        .initialState = RenderGraphBufferState::Undefined,
        .finalState = RenderGraphBufferState::TransferRead,
    });
    const auto readback = graph.importBuffer(RenderGraphBufferDesc{
        .name = "ComputeReadbackBuffer",
        .byteSize = byteSize,
        .initialState = RenderGraphBufferState::Undefined,
        .finalState = RenderGraphBufferState::HostRead,
    });

    std::vector<VulkanRenderGraphImageBinding> imageBindings;
    imageBindings.reserve(1);
    imageBindings.push_back(basicBackbufferBinding(backbuffer, frame));
    auto namedBackbuffer = setVulkanRenderGraphImageDebugNames(frame, imageBindings.back());
    if (!namedBackbuffer) {
        return std::unexpected{std::move(namedBackbuffer.error())};
    }

    std::vector<VulkanRenderGraphBufferBinding> bufferBindings;
    bufferBindings.push_back(VulkanRenderGraphBufferBinding{
        .buffer = storage,
        .vulkanBuffer = storageBuffer_.handle(),
        .offset = 0,
        .size = storageBuffer_.size(),
        .debugName = "ComputeStorageBuffer",
    });
    auto namedStorage = setVulkanRenderGraphBufferDebugName(frame, bufferBindings.back());
    if (!namedStorage) {
        return std::unexpected{std::move(namedStorage.error())};
    }
    bufferBindings.push_back(VulkanRenderGraphBufferBinding{
        .buffer = readback,
        .vulkanBuffer = readbackBuffer_.handle(),
        .offset = 0,
        .size = readbackBuffer_.size(),
        .debugName = "ComputeReadbackBuffer",
    });
    auto namedReadback = setVulkanRenderGraphBufferDebugName(frame, bufferBindings.back());
    if (!namedReadback) {
        return std::unexpected{std::move(namedReadback.error())};
    }

    constexpr BasicTransferFillBufferParams kFillParams{.value = 0};
    graph.addPass("FillStorageBuffer", kBasicTransferFillBufferPassType)
        .setParams(kBasicTransferFillBufferParamsType, kFillParams)
        .writeBuffer("target", storage)
        .recordCommands([](RenderGraphCommandList& commands) {
            commands.fillBuffer("target", kFillParams.value);
        })
        .execute([&frame, &bufferBindings, this](RenderGraphPassContext pass) -> Result<void> {
            [[maybe_unused]] const auto timestamp = VulkanTimestampScope::begin(frame, pass.name);
            [[maybe_unused]] const auto debugLabel = VulkanDebugLabelScope::begin(
                frame, renderGraphPassDebugLabel(pass, {}, bufferBindings));
            auto params = readPassParams<BasicTransferFillBufferParams>(
                pass, kBasicTransferFillBufferParamsType, "Transfer fill buffer pass");
            if (!params) {
                return std::unexpected{std::move(params.error())};
            }
            auto commands = validateBasicTransferFillBufferCommands(pass, *params);
            if (!commands) {
                return std::unexpected{std::move(commands.error())};
            }
            auto transitions = recordRenderGraphBufferTransitions(
                frame, pass.bufferTransitionsBefore, bufferBindings);
            if (!transitions) {
                return std::unexpected{std::move(transitions.error())};
            }
            auto target = findVulkanRenderGraphBufferTransferWrite(pass, "target", bufferBindings);
            if (!target) {
                return std::unexpected{std::move(target.error())};
            }
            vkCmdFillBuffer(frame.commandBuffer, target->vulkanBuffer, target->offset,
                            target->size == VK_WHOLE_SIZE ? storageBuffer_.size() : target->size,
                            params->value);
            ++computeStats_.bufferFillsRecorded;
            return {};
        });

    const BasicTransferClearParams clearParams = basicTransferClearParams(frame.clearColor);
    graph.addPass("ClearBackbuffer", kBasicTransferClearPassType)
        .setParams(kBasicTransferClearParamsType, clearParams)
        .writeTransfer("target", backbuffer)
        .recordCommands([clearParams](RenderGraphCommandList& commands) {
            commands.clearColor("target", clearParams.color);
        })
        .execute([&frame, &imageBindings](RenderGraphPassContext pass) -> Result<void> {
            [[maybe_unused]] const auto timestamp = VulkanTimestampScope::begin(frame, pass.name);
            [[maybe_unused]] const auto debugLabel =
                VulkanDebugLabelScope::begin(frame, renderGraphPassDebugLabel(pass, imageBindings));
            auto transitions =
                recordRenderGraphTransitions(frame, pass.transitionsBefore, imageBindings);
            if (!transitions) {
                return std::unexpected{std::move(transitions.error())};
            }

            auto params = readPassParams<BasicTransferClearParams>(
                pass, kBasicTransferClearParamsType, "Compute dispatch backbuffer clear pass");
            if (!params) {
                return std::unexpected{std::move(params.error())};
            }
            recordTransferClear(frame, basicClearColorValue(*params));
            return {};
        });

    constexpr BasicComputeDispatchParams kDispatchParams{
        .groupCountX = kBasicComputeValueCount,
        .groupCountY = 1,
        .groupCountZ = 1,
    };
    graph.addPass("ComputeDispatch", kBasicComputeDispatchPassType)
        .setParams(kBasicComputeDispatchParamsType, kDispatchParams)
        .readWriteStorageBuffer("target", storage, RenderGraphShaderStage::Compute)
        .recordCommands([](RenderGraphCommandList& commands) {
            commands.setShader("Hidden/BasicCompute", "Main")
                .dispatch(kDispatchParams.groupCountX, kDispatchParams.groupCountY,
                          kDispatchParams.groupCountZ);
        })
        .execute([&frame, &imageBindings, &bufferBindings,
                  this](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicComputeDispatchPass(frame, pass, imageBindings, bufferBindings,
                                                   pipeline_.handle(), pipelineLayout_.handle(),
                                                   descriptorSet_, computeStats_);
        });

    graph.addPass("ComputeReadbackCopy", kBasicComputeReadbackPassType)
        .readTransferBuffer("source", storage)
        .writeBuffer("target", readback)
        .recordCommands([](RenderGraphCommandList& commands) {
            commands.copyBuffer("source", "target");
        })
        .execute(
            [&frame, &imageBindings, &bufferBindings](RenderGraphPassContext pass) -> Result<void> {
                return executeBasicComputeReadbackPass(frame, pass, imageBindings, bufferBindings);
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
        recordRenderGraphTransitions(frame, compiled->finalTransitions, imageBindings);
    if (!finalTransitions) {
        return std::unexpected{std::move(finalTransitions.error())};
    }
    auto finalBufferTransitions =
        recordRenderGraphBufferTransitions(frame, compiled->finalBufferTransitions, bufferBindings);
    if (!finalBufferTransitions) {
        return std::unexpected{std::move(finalBufferTransitions.error())};
    }

    return VulkanFrameRecordResult{
        .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    };
}

Result<std::array<std::uint32_t, 4>>
BasicComputeDispatchRenderer::readbackValuesAfterGpuComplete() {
    std::array<std::uint32_t, kBasicComputeValueCount> values{};
    auto read = readbackBuffer_.read(std::as_writable_bytes(std::span{values}));
    if (!read) {
        return std::unexpected{std::move(read.error())};
    }
    return values;
}

BasicPipelineCacheStats BasicComputeDispatchRenderer::pipelineCacheStats() const {
    return pipelineCacheStats_;
}

VulkanDescriptorAllocatorStats BasicComputeDispatchRenderer::descriptorAllocatorStats() const {
    return descriptorAllocator_.stats();
}

VulkanBufferStats BasicComputeDispatchRenderer::bufferStats() const {
    VulkanBufferStats stats;
    accumulateBufferStats(stats, storageBuffer_);
    accumulateBufferStats(stats, readbackBuffer_);
    return stats;
}

BasicComputeDispatchStats BasicComputeDispatchRenderer::computeStats() const {
    return computeStats_;
}
