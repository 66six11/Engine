struct BasicRenderViewExecutionEventRecorder {
    std::vector<BasicRenderViewExecutionEvent> events;
    std::uint64_t nextEventId{1};

    void append(RenderGraphPassContext pass, BasicRenderViewExecutionEventKind kind,
                std::string label, std::optional<std::size_t> commandIndex = std::nullopt,
                BasicRenderViewDrawEvent draw = {}, BasicRenderViewDispatchEvent dispatch = {},
                std::optional<std::uint32_t> sourceImageResourceIndex = std::nullopt,
                std::optional<std::uint32_t> targetImageResourceIndex = std::nullopt,
                std::optional<std::size_t> sceneDrawItemIndex = std::nullopt,
                std::optional<BasicDrawPacketContext> drawPacketContext = std::nullopt,
                std::optional<std::uint32_t> depthImageResourceIndex = std::nullopt,
                std::optional<std::uint32_t> vertexBufferResourceIndex = std::nullopt,
                std::optional<std::uint32_t> indexBufferResourceIndex = std::nullopt) {
        events.push_back(BasicRenderViewExecutionEvent{
            .id = BasicRenderViewExecutionEventId{.value = nextEventId++},
            .kind = kind,
            .passIndex = pass.passIndex,
            .declarationIndex = pass.declarationIndex,
            .commandIndex = commandIndex,
            .passName = std::string{pass.name},
            .label = std::move(label),
            .draw = draw,
            .dispatch = dispatch,
            .sceneDrawItemIndex = sceneDrawItemIndex,
            .drawPacketContext = drawPacketContext,
            .sourceImageResourceIndex = sourceImageResourceIndex,
            .targetImageResourceIndex = targetImageResourceIndex,
            .depthImageResourceIndex = depthImageResourceIndex,
            .vertexBufferResourceIndex = vertexBufferResourceIndex,
            .indexBufferResourceIndex = indexBufferResourceIndex,
        });
    }

    void beginPass(RenderGraphPassContext pass) {
        append(pass, BasicRenderViewExecutionEventKind::BeginPass,
               std::string{"Begin "} + std::string{pass.name});
    }

    void endPass(RenderGraphPassContext pass) {
        append(pass, BasicRenderViewExecutionEventKind::EndPass,
               std::string{"End "} + std::string{pass.name});
    }
};

[[nodiscard]] std::optional<std::size_t> firstCommandIndex(RenderGraphPassContext pass,
                                                           RenderGraphCommandKind kind) {
    for (std::size_t index = 0; index < pass.commands.size(); ++index) {
        if (pass.commands[index].kind == kind) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::string>
basicRenderViewSourceOverlayIds(std::span<const std::string_view> ids) {
    std::vector<std::string> copied;
    copied.reserve(ids.size());
    for (std::string_view id : ids) {
        copied.emplace_back(id);
    }
    return copied;
}

[[nodiscard]] std::vector<BasicDrawPacketContext>
basicRenderViewDrawPacketContexts(std::span<const BasicDrawListItem> drawItems) {
    std::vector<BasicDrawPacketContext> contexts;
    contexts.reserve(drawItems.size());
    for (const BasicDrawListItem& item : drawItems) {
        contexts.push_back(item.context);
    }
    return contexts;
}

void setBasicRenderViewWireframeUnavailableDiagnostics(const BasicRenderViewDesc& view) {
    if (view.diagnostics == nullptr) {
        return;
    }

    *view.diagnostics = BasicRenderViewDiagnostics{
        .viewName = std::string{view.viewName},
        .viewKind = view.viewKind,
        .camera = view.camera,
        .frameParams = view.frameParams,
        .scene =
            BasicRenderViewSceneDiagnostics{
                .sourceRevision = view.scene.sourceRevision,
                .drawItemCount = static_cast<std::uint64_t>(view.scene.drawItems.size()),
                .indexedDrawCount = static_cast<std::uint64_t>(view.scene.drawItems.size()),
                .rasterMode = view.scene.rasterMode,
                .wireframePath = BasicSceneWireframePath::Unavailable,
                .meshResource = kBasicValidationMeshResourceKey,
                .materialResource = kBasicDefaultUnlitMaterialResourceKey,
                .drawPacketContexts = basicRenderViewDrawPacketContexts(view.scene.drawItems),
            },
        .overlay = {},
        .renderGraph = {},
        .executionEvents = {},
    };
}

void setBasicRenderViewDiagnostics(const BasicRenderViewDesc& view, const RenderGraph& graph,
                                   const RenderGraphCompileResult& compiled,
                                   BasicRenderViewExecutionEventRecorder& eventRecorder) {
    if (view.diagnostics == nullptr) {
        return;
    }

    std::vector<std::string> sourceOverlayIds =
        basicRenderViewSourceOverlayIds(view.overlay.sourceOverlayIds);
    std::vector<BasicDrawPacketContext> drawPacketContexts =
        basicRenderViewDrawPacketContexts(view.scene.drawItems);
    std::uint64_t indexedDrawCount{};
    for (const BasicDrawListItem& item : view.scene.drawItems) {
        if (item.drawItem.indexCount > 0U) {
            ++indexedDrawCount;
        }
    }
    const bool hasSceneMesh = !view.scene.drawItems.empty();
    *view.diagnostics = BasicRenderViewDiagnostics{
        .viewName = std::string{view.viewName},
        .viewKind = view.viewKind,
        .camera = view.camera,
        .frameParams = view.frameParams,
        .scene =
            BasicRenderViewSceneDiagnostics{
                .sourceRevision = view.scene.sourceRevision,
                .drawItemCount = static_cast<std::uint64_t>(view.scene.drawItems.size()),
                .indexedDrawCount = indexedDrawCount,
                .rasterMode = view.scene.rasterMode,
                .wireframePath =
                    hasSceneMesh && view.scene.rasterMode == BasicSceneRasterMode::Wireframe
                        ? BasicSceneWireframePath::PolygonLine
                        : BasicSceneWireframePath::NotRequested,
                .meshResource =
                    hasSceneMesh ? kBasicValidationMeshResourceKey : BasicDrawResourceKey{},
                .materialResource =
                    hasSceneMesh ? kBasicDefaultUnlitMaterialResourceKey : BasicDrawResourceKey{},
                .drawPacketContexts = std::move(drawPacketContexts),
            },
        .overlay =
            BasicRenderViewOverlayDiagnostics{
                .enabled = view.overlay.enabled,
                .colorLoadOp = view.overlay.colorLoadOp,
                .colorStoreOp = view.overlay.colorStoreOp,
                .blendMode = view.overlay.blendMode,
                .worldGridEnabled = view.overlay.worldGrid.enabled,
                .worldGrid = view.overlay.worldGrid,
                .selectionOutlineEnabled =
                    !view.overlay.selectionOutline.drawItems.empty(),
                .selectionOutlineDrawItemCount = static_cast<std::uint64_t>(
                    view.overlay.selectionOutline.drawItems.size()),
                .debugWorldLineCount =
                    static_cast<std::uint64_t>(view.overlay.debugWorldLines.size()),
                .sourceOverlayIds = std::move(sourceOverlayIds),
            },
        .renderGraph = graph.diagnosticsSnapshot(compiled),
        .executionEvents = std::move(eventRecorder.events),
    };
}
