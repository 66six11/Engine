    BasicTriangleRenderer::BasicTriangleRenderer(BasicTriangleRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicTriangleRenderer&
    BasicTriangleRenderer::operator=(BasicTriangleRenderer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        vertexShader_ = std::move(other.vertexShader_);
        fragmentShader_ = std::move(other.fragmentShader_);
        descriptorSetLayouts_ = std::move(other.descriptorSetLayouts_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipelineCache_ = std::move(other.pipelineCache_);
        pipeline_ = std::move(other.pipeline_);
        vertexBuffer_ = std::move(other.vertexBuffer_);
        indexBuffer_ = std::move(other.indexBuffer_);
        transientImagePool_ = std::move(other.transientImagePool_);
        transientImages_ = std::move(other.transientImages_);
        pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
        pipelineDepthFormat_ = std::exchange(other.pipelineDepthFormat_, VK_FORMAT_UNDEFINED);
        pipelineCacheStats_ = std::exchange(other.pipelineCacheStats_, {});
        allocator_ = std::exchange(other.allocator_, nullptr);
        drawItem_ = std::exchange(other.drawItem_, basicTriangleDrawItem());
        return *this;
    }

    Result<BasicTriangleRenderer>
    BasicTriangleRenderer::create(const BasicTriangleRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create triangle renderer without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{Error{ErrorDomain::Vulkan, 0,
                                         "Cannot create triangle renderer without an allocator"}};
        }
        const BasicDrawItem drawItem = desc.meshKind == BasicMeshKind::IndexedQuad
                                           ? basicIndexedQuadDrawItem()
                                           : desc.drawItem;
        if ((drawItem.vertexCount == 0 && drawItem.indexCount == 0) ||
            drawItem.instanceCount == 0) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Triangle renderer draw item must draw something"}};
        }

        auto vertexCode = readSpirvFile(desc.shaderDirectory / "basic_triangle.vert.spv");
        if (!vertexCode) {
            return std::unexpected{std::move(vertexCode.error())};
        }
        auto fragmentCode = readSpirvFile(desc.shaderDirectory / "basic_triangle.frag.spv");
        if (!fragmentCode) {
            return std::unexpected{std::move(fragmentCode.error())};
        }

        auto reflection = validateBasicTriangleReflection(desc.shaderDirectory);
        if (!reflection) {
            return std::unexpected{std::move(reflection.error())};
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

        auto layoutResources = createPipelineLayoutResources(desc.device, *reflection);
        if (!layoutResources) {
            return std::unexpected{std::move(layoutResources.error())};
        }
        auto pipelineCache = createBasicPipelineCache(desc.device);
        if (!pipelineCache) {
            return std::unexpected{std::move(pipelineCache.error())};
        }

        constexpr auto triangleVertices = basicTriangleVertices();
        constexpr auto quadVertices = basicQuadVertices();
        const std::span<const BasicVertex> vertices =
            desc.meshKind == BasicMeshKind::IndexedQuad
                ? std::span<const BasicVertex>{quadVertices}
                : std::span<const BasicVertex>{triangleVertices};
        auto vertexBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = vertices.size_bytes(),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!vertexBuffer) {
            return std::unexpected{std::move(vertexBuffer.error())};
        }
        auto uploaded = vertexBuffer->upload(std::as_bytes(vertices));
        if (!uploaded) {
            return std::unexpected{std::move(uploaded.error())};
        }

        VulkanBuffer indexBuffer;
        if (drawItem.indexCount > 0) {
            constexpr auto indices = basicQuadIndices();
            auto createdIndexBuffer = VulkanBuffer::create(VulkanBufferDesc{
                .device = desc.device,
                .allocator = desc.allocator,
                .size = sizeof(indices),
                .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
            });
            if (!createdIndexBuffer) {
                return std::unexpected{std::move(createdIndexBuffer.error())};
            }
            auto uploadedIndices = createdIndexBuffer->upload(std::as_bytes(std::span{indices}));
            if (!uploadedIndices) {
                return std::unexpected{std::move(uploadedIndices.error())};
            }
            indexBuffer = std::move(*createdIndexBuffer);
        }

        BasicTriangleRenderer renderer;
        renderer.device_ = desc.device;
        renderer.allocator_ = desc.allocator;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.descriptorSetLayouts_ = std::move(layoutResources->descriptorSetLayouts);
        renderer.pipelineLayout_ = std::move(layoutResources->pipelineLayout);
        renderer.pipelineCache_ = std::move(*pipelineCache);
        renderer.vertexBuffer_ = std::move(*vertexBuffer);
        renderer.indexBuffer_ = std::move(indexBuffer);
        renderer.drawItem_ = drawItem;
        return renderer;
    }

    Result<void> BasicTriangleRenderer::ensurePipeline(VkFormat colorFormat, VkFormat depthFormat) {
        if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat &&
            pipelineDepthFormat_ == depthFormat) {
            ++pipelineCacheStats_.reused;
            return {};
        }

        const auto bindings = basicVertexInputBindings();
        const auto attributes = basicVertexInputAttributes();

        auto pipeline = VulkanGraphicsPipeline::createDynamicRendering(VulkanGraphicsPipelineDesc{
            .device = device_,
            .pipelineCache = pipelineCache_.handle(),
            .layout = pipelineLayout_.handle(),
            .vertexShader = vertexShader_.handle(),
            .fragmentShader = fragmentShader_.handle(),
            .vertexEntryPoint = "main",
            .fragmentEntryPoint = "main",
            .colorFormat = colorFormat,
            .depthFormat = depthFormat,
            .vertexBindings = bindings,
            .vertexAttributes = attributes,
        });
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        pipeline_ = std::move(*pipeline);
        pipelineFormat_ = colorFormat;
        pipelineDepthFormat_ = depthFormat;
        ++pipelineCacheStats_.created;
        return {};
    }

    BasicPipelineCacheStats BasicTriangleRenderer::pipelineCacheStats() const {
        return pipelineCacheStats_;
    }

    VulkanBufferStats BasicTriangleRenderer::bufferStats() const {
        VulkanBufferStats stats;
        accumulateBufferStats(stats, vertexBuffer_);
        accumulateBufferStats(stats, indexBuffer_);
        return stats;
    }

    Result<VulkanFrameRecordResult>
    BasicTriangleRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
        auto pipeline = ensurePipeline(frame.format);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const std::array bindings{basicBackbufferBinding(backbuffer, frame)};
        const BasicTransferClearParams clearParams = basicTransferClearParams(frame.clearColor);

        graph.addPass("ClearColor", kBasicTransferClearPassType)
            .setParams(kBasicTransferClearParamsType, clearParams)
            .writeTransfer("target", backbuffer)
            .recordCommands([clearParams](RenderGraphCommandList& commands) {
                commands.clearColor("target", clearParams.color);
            })
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                [[maybe_unused]] const auto timestamp =
                    VulkanTimestampScope::begin(frame, pass.name);
                [[maybe_unused]] const auto debugLabel =
                    VulkanDebugLabelScope::begin(frame, pass.name);
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }
                auto clearParams = readPassParams<BasicTransferClearParams>(
                    pass, kBasicTransferClearParamsType, "Triangle clear pass");
                if (!clearParams) {
                    return std::unexpected{std::move(clearParams.error())};
                }
                recordTransferClear(frame, basicClearColorValue(*clearParams));
                return {};
            });

        graph.addPass("Triangle", kBasicRasterTrianglePassType)
            .setParams(kBasicRasterTriangleParamsType, drawItem_)
            .writeColor("target", backbuffer)
            .execute([&frame, &bindings, this](RenderGraphPassContext pass) -> Result<void> {
                [[maybe_unused]] const auto timestamp =
                    VulkanTimestampScope::begin(frame, pass.name);
                [[maybe_unused]] const auto debugLabel =
                    VulkanDebugLabelScope::begin(frame, pass.name);
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }
                auto drawItem = readPassParams<BasicDrawItem>(pass, kBasicRasterTriangleParamsType,
                                                              "Triangle raster pass");
                if (!drawItem) {
                    return std::unexpected{std::move(drawItem.error())};
                }
                recordTriangleDraw(frame, pipeline_.handle(),
                                   BasicDrawBuffers{
                                       .vertex = vertexBuffer_.handle(),
                                       .index = indexBuffer_.handle(),
                                   },
                                   *drawItem);
                return {};
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
            .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }

    Result<VulkanFrameRecordResult>
    BasicTriangleRenderer::recordFrameWithDepth(const VulkanFrameRecordContext& frame) {
        constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
        auto pipeline = ensurePipeline(frame.format, kDepthFormat);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const auto depth = graph.createTransientImage(RenderGraphImageDesc{
            .name = "DepthBuffer",
            .format = RenderGraphImageFormat::D32Sfloat,
            .extent = basicRenderGraphExtent(frame.extent),
        });

        std::vector<VulkanRenderGraphImageBinding> bindings;
        bindings.reserve(2);
        bindings.push_back(basicBackbufferBinding(backbuffer, frame));
        const BasicTransferClearParams clearParams = basicTransferClearParams(frame.clearColor);

        graph.addPass("ClearColor", kBasicTransferClearPassType)
            .setParams(kBasicTransferClearParamsType, clearParams)
            .writeTransfer("target", backbuffer)
            .recordCommands([clearParams](RenderGraphCommandList& commands) {
                commands.clearColor("target", clearParams.color);
            })
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                [[maybe_unused]] const auto timestamp =
                    VulkanTimestampScope::begin(frame, pass.name);
                [[maybe_unused]] const auto debugLabel =
                    VulkanDebugLabelScope::begin(frame, pass.name);
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }
                auto clearParams = readPassParams<BasicTransferClearParams>(
                    pass, kBasicTransferClearParamsType, "Depth triangle clear pass");
                if (!clearParams) {
                    return std::unexpected{std::move(clearParams.error())};
                }
                recordTransferClear(frame, basicClearColorValue(*clearParams));
                return {};
            });

        graph.addPass("DepthTriangle", kBasicRasterDepthTrianglePassType)
            .setParams(kBasicRasterDepthTriangleParamsType, drawItem_)
            .writeColor("target", backbuffer)
            .writeDepth("depth", depth)
            .execute([&frame, &bindings, this](RenderGraphPassContext pass) -> Result<void> {
                [[maybe_unused]] const auto timestamp =
                    VulkanTimestampScope::begin(frame, pass.name);
                [[maybe_unused]] const auto debugLabel =
                    VulkanDebugLabelScope::begin(frame, pass.name);
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto depthBinding = findVulkanRenderGraphDepthWrite(pass, "depth", bindings);
                if (!depthBinding) {
                    return std::unexpected{std::move(depthBinding.error())};
                }
                auto drawItem = readPassParams<BasicDrawItem>(
                    pass, kBasicRasterDepthTriangleParamsType, "Depth triangle raster pass");
                if (!drawItem) {
                    return std::unexpected{std::move(drawItem.error())};
                }

                recordTriangleDraw(frame, pipeline_.handle(),
                                   BasicDrawBuffers{
                                       .vertex = vertexBuffer_.handle(),
                                       .index = indexBuffer_.handle(),
                                   },
                                   *drawItem, depthBinding->vulkanImageView);
                return {};
            });

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

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }
