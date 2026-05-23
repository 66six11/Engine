        [[nodiscard]] Result<void> compileExecuteAndFinalizeBasicGraph(
            const VulkanFrameRecordContext& frame, VkDevice device, VmaAllocator allocator,
            RenderGraph& graph, std::vector<VulkanRenderGraphImageBinding>& imageBindings,
            VulkanTransientImagePool& transientImagePool,
            std::vector<VulkanTransientImageResource>& transientImages) {
            const RenderGraphSchemaRegistry schemas = basicRenderGraphSchemaRegistry();
            auto compiled = graph.compile(schemas);
            if (!compiled) {
                return std::unexpected{std::move(compiled.error())};
            }

            auto prepared = prepareTransientResources(frame, device, allocator, *compiled,
                                                      imageBindings, transientImagePool,
                                                      transientImages);
            if (!prepared) {
                return std::unexpected{std::move(prepared.error())};
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

            return {};
        }
