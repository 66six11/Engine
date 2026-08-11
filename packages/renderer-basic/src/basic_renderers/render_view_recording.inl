struct BasicRenderViewPassRecordingContext {
    RenderGraph& graph;
    RenderGraphImageHandle renderTarget{};
    RenderGraphImageHandle sceneDepth{};
    RenderGraphBufferHandle sceneVertices{};
    RenderGraphBufferHandle sceneIndices{};
    const BasicRenderViewPassPolicy& policy;
    const VulkanFrameRecordContext& frame;
    std::vector<VulkanRenderGraphImageBinding>& bindings;
    std::vector<VulkanRenderGraphBufferBinding>& bufferBindings;
    BasicRenderViewTarget viewTarget{};
    BasicRenderViewCamera camera{};
    std::span<const BasicDrawListItem> sceneDrawItems;
    BasicSceneRasterMode sceneRasterMode{BasicSceneRasterMode::Solid};
    BasicRenderViewOverlayColorLoadOp colorLoadOp{
        BasicRenderViewOverlayColorLoadOp::LoadSceneColor};
    BasicRenderViewOverlayColorStoreOp colorStoreOp{BasicRenderViewOverlayColorStoreOp::Store};
    BasicRenderViewExecutionEventRecorder& eventRecorder;
};

void addBasicRenderViewWorldGridPass(const BasicRenderViewPassRecordingContext& context,
                                     VkPipeline worldGridPipeline,
                                     VkPipelineLayout worldGridPipelineLayout) {
    context.graph.addPass("RenderViewWorldGrid", kBasicRenderViewWorldGridPassType)
        .setParams(kBasicRenderViewWorldGridParamsType, context.policy.worldGridParams)
        .readWriteColor("target", context.renderTarget)
        .recordCommands(
            [worldGridParams = context.policy.worldGridParams](RenderGraphCommandList& commands) {
                commands.setShader("Hidden/RenderViewWorldGrid", "Fullscreen")
                    .setVec4("CameraPositionNear", worldGridParams.cameraPositionNear)
                    .setVec4("ViewportFade", worldGridParams.viewportFade)
                    .setVec4("GridSettings", worldGridParams.gridSettings)
                    .setVec4("GridLodSettings", worldGridParams.gridLodSettings)
                    .setVec4("GridColor", worldGridParams.gridColor)
                    .drawFullscreenTriangle();
            })
        .execute(
            [&frame = context.frame, &bindings = context.bindings, viewTarget = context.viewTarget,
             camera = context.camera, colorLoadOp = context.colorLoadOp,
             colorStoreOp = context.colorStoreOp, worldGridPipeline, worldGridPipelineLayout,
             &eventRecorder = context.eventRecorder](RenderGraphPassContext pass) -> Result<void> {
                return executeBasicRenderViewWorldGridPass(
                    frame, pass, bindings, viewTarget.extent, camera, colorLoadOp, colorStoreOp,
                    worldGridPipeline, worldGridPipelineLayout, &eventRecorder);
            });
}

void addBasicRenderViewSceneMeshPass(const BasicRenderViewPassRecordingContext& context,
                                     VkPipeline sceneMeshPipeline,
                                     VkPipelineLayout sceneMeshPipelineLayout) {
    context.graph.addPass("RenderViewSceneMesh", kBasicRenderViewSceneMeshPassType)
        .setParams(kBasicRenderViewSceneMeshParamsType, context.policy.sceneMeshParams)
        .readWriteColor("target", context.renderTarget)
        .writeDepth("depth", context.sceneDepth)
        .readVertexBuffer("vertices", context.sceneVertices)
        .readIndexBuffer("indices", context.sceneIndices)
        .recordCommands([sceneMeshParams = context.policy.sceneMeshParams,
                         drawItems = context.sceneDrawItems](RenderGraphCommandList& commands) {
            commands.setShader("Hidden/RenderViewSceneMesh", "DefaultUnlit")
                .setInt("SceneDrawItemCount", static_cast<int>(sceneMeshParams.drawItemCount))
                .setInt("SceneRasterMode", static_cast<int>(sceneMeshParams.rasterMode));
            for (const BasicDrawListItem& item : drawItems) {
                commands.drawIndexed(item.drawItem.indexCount, item.drawItem.instanceCount,
                                     item.drawItem.firstIndex, item.drawItem.vertexOffset,
                                     item.drawItem.firstInstance);
            }
        })
        .execute(
            [&frame = context.frame, &bindings = context.bindings,
             &bufferBindings = context.bufferBindings, viewTarget = context.viewTarget,
             camera = context.camera, rasterMode = context.sceneRasterMode, sceneMeshPipeline,
             sceneMeshPipelineLayout, drawItems = context.sceneDrawItems,
             &eventRecorder = context.eventRecorder](RenderGraphPassContext pass) -> Result<void> {
                return executeBasicRenderViewSceneMeshPass(
                    frame, pass, bindings, bufferBindings, viewTarget.extent, camera, rasterMode,
                    sceneMeshPipeline, sceneMeshPipelineLayout, drawItems, &eventRecorder);
            });
}

void addBasicRenderViewOverlayPass(const BasicRenderViewPassRecordingContext& context,
                                   VkPipeline debugLinePipeline, VkBuffer debugLineVertexBuffer,
                                   std::uint32_t debugLineVertexCount) {
    context.graph.addPass("RenderViewOverlayInputs", kBasicRenderViewOverlayPassType)
        .setParams(kBasicRenderViewOverlayParamsType, context.policy.overlayParams)
        .readWriteColor("target", context.renderTarget)
        .recordCommands([overlayParams =
                             context.policy.overlayParams](RenderGraphCommandList& commands) {
            commands.setShader("Hidden/RenderViewOverlay", "Inputs")
                .setVec4("CameraPositionNear", overlayParams.cameraPositionNear)
                .setVec4("FrameTimeScale", overlayParams.frameTimeScale)
                .setInt("DebugWorldLineCount", static_cast<int>(overlayParams.debugWorldLineCount));
        })
        .execute(
            [&frame = context.frame, &bindings = context.bindings, viewTarget = context.viewTarget,
             colorLoadOp = context.colorLoadOp, colorStoreOp = context.colorStoreOp,
             debugLinePipeline, debugLineVertexBuffer, debugLineVertexCount,
             &eventRecorder = context.eventRecorder](RenderGraphPassContext pass) -> Result<void> {
                return executeBasicRenderViewOverlayPass(
                    frame, pass, bindings, viewTarget.extent, colorLoadOp, colorStoreOp,
                    debugLinePipeline, debugLineVertexBuffer, debugLineVertexCount, &eventRecorder);
            });
}
