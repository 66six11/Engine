    BasicDrawListRenderer::BasicDrawListRenderer(BasicDrawListRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicDrawListRenderer&
    BasicDrawListRenderer::operator=(BasicDrawListRenderer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        vertexShader_ = std::move(other.vertexShader_);
        fragmentShader_ = std::move(other.fragmentShader_);
        pipelineLayout_ = std::move(other.pipelineLayout_);
        pipelineCache_ = std::move(other.pipelineCache_);
        pipeline_ = std::move(other.pipeline_);
        vertexBuffer_ = std::move(other.vertexBuffer_);
        indexBuffer_ = std::move(other.indexBuffer_);
        pipelineFormat_ = std::exchange(other.pipelineFormat_, VK_FORMAT_UNDEFINED);
        pipelineDepthFormat_ = std::exchange(other.pipelineDepthFormat_, VK_FORMAT_UNDEFINED);
        pipelineCacheStats_ = std::exchange(other.pipelineCacheStats_, {});
        drawItems_ = std::move(other.drawItems_);
        transientImagePool_ = std::move(other.transientImagePool_);
        transientImages_ = std::move(other.transientImages_);
        allocator_ = std::exchange(other.allocator_, nullptr);
        return *this;
    }

    Result<BasicDrawListRenderer>
    BasicDrawListRenderer::create(const BasicDrawListRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create draw list renderer without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{Error{ErrorDomain::Vulkan, 0,
                                         "Cannot create draw list renderer without an allocator"}};
        }

        constexpr auto defaultDrawItems = basicDrawListSmokeItems();
        const std::span<const BasicDrawListItem> drawItems =
            desc.drawItems.empty() ? std::span<const BasicDrawListItem>{defaultDrawItems.data(),
                                                                        defaultDrawItems.size()}
                                   : desc.drawItems;
        auto drawListValidated = validateBasicDrawListItems(drawItems);
        if (!drawListValidated) {
            return std::unexpected{std::move(drawListValidated.error())};
        }

        auto vertexCode = readSpirvFile(desc.shaderDirectory / "basic_mesh3d.vert.spv");
        if (!vertexCode) {
            return std::unexpected{std::move(vertexCode.error())};
        }
        auto fragmentCode = readSpirvFile(desc.shaderDirectory / "basic_mesh3d.frag.spv");
        if (!fragmentCode) {
            return std::unexpected{std::move(fragmentCode.error())};
        }

        auto reflectionValidated = validateMesh3DReflection(desc.shaderDirectory);
        if (!reflectionValidated) {
            return std::unexpected{std::move(reflectionValidated.error())};
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

        constexpr std::array pushConstantRanges{
            VkPushConstantRange{
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .offset = 0,
                .size = static_cast<std::uint32_t>(sizeof(BasicMesh3DPushConstants)),
            },
        };
        auto pipelineLayout = VulkanPipelineLayout::create(VulkanPipelineLayoutDesc{
            .device = desc.device,
            .setLayouts = {},
            .pushConstantRanges = pushConstantRanges,
        });
        if (!pipelineLayout) {
            return std::unexpected{std::move(pipelineLayout.error())};
        }
        auto pipelineCache = createBasicPipelineCache(desc.device);
        if (!pipelineCache) {
            return std::unexpected{std::move(pipelineCache.error())};
        }

        constexpr auto vertices = basicCubeVertices();
        auto vertexBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = sizeof(vertices),
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!vertexBuffer) {
            return std::unexpected{std::move(vertexBuffer.error())};
        }
        auto uploadedVertices = vertexBuffer->upload(std::as_bytes(std::span{vertices}));
        if (!uploadedVertices) {
            return std::unexpected{std::move(uploadedVertices.error())};
        }

        constexpr auto indices = basicCubeIndices();
        auto indexBuffer = VulkanBuffer::create(VulkanBufferDesc{
            .device = desc.device,
            .allocator = desc.allocator,
            .size = sizeof(indices),
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .memoryUsage = VulkanBufferMemoryUsage::HostUpload,
        });
        if (!indexBuffer) {
            return std::unexpected{std::move(indexBuffer.error())};
        }
        auto uploadedIndices = indexBuffer->upload(std::as_bytes(std::span{indices}));
        if (!uploadedIndices) {
            return std::unexpected{std::move(uploadedIndices.error())};
        }

        BasicDrawListRenderer renderer;
        renderer.device_ = desc.device;
        renderer.allocator_ = desc.allocator;
        renderer.vertexShader_ = std::move(*vertexShader);
        renderer.fragmentShader_ = std::move(*fragmentShader);
        renderer.pipelineLayout_ = std::move(*pipelineLayout);
        renderer.pipelineCache_ = std::move(*pipelineCache);
        renderer.vertexBuffer_ = std::move(*vertexBuffer);
        renderer.indexBuffer_ = std::move(*indexBuffer);
        renderer.drawItems_.assign(drawItems.begin(), drawItems.end());
        return renderer;
    }

    Result<void> BasicDrawListRenderer::ensurePipeline(VkFormat colorFormat, VkFormat depthFormat) {
        if (pipeline_.handle() != VK_NULL_HANDLE && pipelineFormat_ == colorFormat &&
            pipelineDepthFormat_ == depthFormat) {
            ++pipelineCacheStats_.reused;
            return {};
        }

        const auto bindings = basicVertex3DInputBindings();
        const auto attributes = basicVertex3DInputAttributes();

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

    BasicPipelineCacheStats BasicDrawListRenderer::pipelineCacheStats() const {
        return pipelineCacheStats_;
    }

    VulkanBufferStats BasicDrawListRenderer::bufferStats() const {
        VulkanBufferStats stats;
        accumulateBufferStats(stats, vertexBuffer_);
        accumulateBufferStats(stats, indexBuffer_);
        return stats;
    }

    Result<VulkanFrameRecordResult>
    BasicDrawListRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
        constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
        auto pipeline = ensurePipeline(frame.format, kDepthFormat);
        if (!pipeline) {
            return std::unexpected{std::move(pipeline.error())};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const auto depth = graph.createTransientImage(RenderGraphImageDesc{
            .name = "DrawListDepthBuffer",
            .format = RenderGraphImageFormat::D32Sfloat,
            .extent = basicRenderGraphExtent(frame.extent),
        });
        const BasicDrawListParams drawListParams{
            .drawCount = static_cast<std::uint32_t>(drawItems_.size()),
        };
        const BasicTransferClearParams clearParams = basicTransferClearParams(frame.clearColor);

        std::vector<VulkanRenderGraphImageBinding> bindings;
        bindings.reserve(2);
        bindings.push_back(basicBackbufferBinding(backbuffer, frame));

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
                    pass, kBasicTransferClearParamsType, "Draw list clear pass");
                if (!clearParams) {
                    return std::unexpected{std::move(clearParams.error())};
                }
                const VkClearColorValue clearColor = basicClearColorValue(*clearParams);
                recordTransferClear(frame, clearColor);
                return {};
            });

        graph.addPass("DrawList", kBasicRasterDrawListPassType)
            .setParams(kBasicRasterDrawListParamsType, drawListParams)
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

                auto drawListParams = readPassParams<BasicDrawListParams>(
                    pass, kBasicRasterDrawListParamsType, "Draw list pass");
                if (!drawListParams) {
                    return std::unexpected{std::move(drawListParams.error())};
                }
                if (static_cast<std::size_t>(drawListParams->drawCount) != drawItems_.size()) {
                    return std::unexpected{renderGraphError(
                        "Draw list pass params draw count does not match renderer draw list")};
                }

                auto depthBinding = findVulkanRenderGraphDepthWrite(pass, "depth", bindings);
                if (!depthBinding) {
                    return std::unexpected{std::move(depthBinding.error())};
                }

                recordDrawListDraw(frame, pipeline_.handle(), pipelineLayout_.handle(),
                                   BasicDrawBuffers{
                                       .vertex = vertexBuffer_.handle(),
                                       .index = indexBuffer_.handle(),
                                   },
                                   depthBinding->vulkanImageView, drawItems_);
                return {};
            });

        auto recorded = compileExecuteAndFinalizeBasicGraph(
            frame, device_, allocator_, graph, bindings, transientImagePool_, transientImages_);
        if (!recorded) {
            return std::unexpected{std::move(recorded.error())};
        }

        return VulkanFrameRecordResult{
            .waitStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
    }
