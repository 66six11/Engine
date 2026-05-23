    BasicMrtRenderer::BasicMrtRenderer(BasicMrtRenderer&& other) noexcept {
        *this = std::move(other);
    }

    BasicMrtRenderer& BasicMrtRenderer::operator=(BasicMrtRenderer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        allocator_ = std::exchange(other.allocator_, nullptr);
        transientImagePool_ = std::move(other.transientImagePool_);
        transientImages_ = std::move(other.transientImages_);
        return *this;
    }

    Result<BasicMrtRenderer> BasicMrtRenderer::create(const BasicMrtRendererDesc& desc) {
        if (desc.device == VK_NULL_HANDLE) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create MRT renderer without a device"}};
        }
        if (desc.allocator == nullptr) {
            return std::unexpected{
                Error{ErrorDomain::Vulkan, 0, "Cannot create MRT renderer without an allocator"}};
        }

        BasicMrtRenderer renderer;
        renderer.device_ = desc.device;
        renderer.allocator_ = desc.allocator;
        return renderer;
    }

    VulkanTransientImagePoolStats BasicMrtRenderer::transientPoolStats() const {
        return transientImagePool_.stats();
    }

    Result<VulkanFrameRecordResult>
    BasicMrtRenderer::recordFrame(const VulkanFrameRecordContext& frame) {
        const RenderGraphImageFormat colorFormat = basicRenderGraphImageFormat(frame.format);
        if (colorFormat == RenderGraphImageFormat::Undefined) {
            return std::unexpected{
                renderGraphError("MRT renderer does not support the current swapchain format")};
        }

        RenderGraph graph;
        const auto backbuffer = graph.importImage(basicBackbufferDesc(frame));
        const RenderGraphExtent2D graphExtent = basicRenderGraphExtent(frame.extent);
        const auto color0 = graph.createTransientImage(RenderGraphImageDesc{
            .name = "MrtColor0",
            .format = colorFormat,
            .extent = graphExtent,
        });
        const auto color1 = graph.createTransientImage(RenderGraphImageDesc{
            .name = "MrtColor1",
            .format = colorFormat,
            .extent = graphExtent,
        });
        const BasicTransferClearParams clearParams = basicTransferClearParams(frame.clearColor);

        std::vector<VulkanRenderGraphImageBinding> bindings;
        bindings.reserve(3);
        bindings.push_back(basicBackbufferBinding(backbuffer, frame));
        auto namedBackbuffer = setVulkanRenderGraphImageDebugNames(frame, bindings.back());
        if (!namedBackbuffer) {
            return std::unexpected{std::move(namedBackbuffer.error())};
        }

        graph.addPass("ClearBackbuffer", kBasicTransferClearPassType)
            .setParams(kBasicTransferClearParamsType, clearParams)
            .writeTransfer("target", backbuffer)
            .recordCommands([clearParams](RenderGraphCommandList& commands) {
                commands.clearColor("target", clearParams.color);
            })
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                [[maybe_unused]] const auto timestamp =
                    VulkanTimestampScope::begin(frame, pass.name);
                [[maybe_unused]] const auto debugLabel =
                    VulkanDebugLabelScope::begin(frame, renderGraphPassDebugLabel(pass, bindings));
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto params = readPassParams<BasicTransferClearParams>(
                    pass, kBasicTransferClearParamsType, "MRT backbuffer clear pass");
                if (!params) {
                    return std::unexpected{std::move(params.error())};
                }
                recordTransferClear(frame, basicClearColorValue(*params));
                return {};
            });

        graph.addPass("MrtClear", kBasicRasterMrtPassType)
            .setParamsType(kBasicRasterMrtParamsType)
            .writeColor("color0", color0)
            .writeColor("color1", color1)
            .recordCommands([](RenderGraphCommandList& commands) {
                commands.clearColor("color0", std::array{0.72F, 0.18F, 0.10F, 1.0F})
                    .clearColor("color1", std::array{0.08F, 0.34F, 0.78F, 1.0F});
            })
            .execute([&frame, &bindings](RenderGraphPassContext pass) -> Result<void> {
                [[maybe_unused]] const auto timestamp =
                    VulkanTimestampScope::begin(frame, pass.name);
                [[maybe_unused]] const auto debugLabel =
                    VulkanDebugLabelScope::begin(frame, renderGraphPassDebugLabel(pass, bindings));
                auto transitions =
                    recordRenderGraphTransitions(frame, pass.transitionsBefore, bindings);
                if (!transitions) {
                    return std::unexpected{std::move(transitions.error())};
                }

                auto color0Binding = findVulkanRenderGraphColorWrite(pass, "color0", bindings);
                if (!color0Binding) {
                    return std::unexpected{std::move(color0Binding.error())};
                }
                auto color1Binding = findVulkanRenderGraphColorWrite(pass, "color1", bindings);
                if (!color1Binding) {
                    return std::unexpected{std::move(color1Binding.error())};
                }
                auto clearValues = basicMrtClearValues(pass);
                if (!clearValues) {
                    return std::unexpected{std::move(clearValues.error())};
                }

                recordMrtClear(frame, std::array{*color0Binding, *color1Binding}, *clearValues);
                return {};
            });

        auto recorded = compileExecuteAndFinalizeBasicGraph(
            frame, device_, allocator_, graph, bindings, transientImagePool_, transientImages_);
        if (!recorded) {
            return std::unexpected{std::move(recorded.error())};
        }

        return VulkanFrameRecordResult{
            .waitStageMask =
                VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    }
