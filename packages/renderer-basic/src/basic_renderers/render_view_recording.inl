struct BasicRenderViewPassRecordingContext {
    RenderGraph& graph;
    RenderGraphImageHandle renderTarget{};
    const BasicRenderViewPassPolicy& policy;
    const VulkanFrameRecordContext& frame;
    std::vector<VulkanRenderGraphImageBinding>& bindings;
    BasicRenderViewTarget viewTarget{};
    BasicRenderViewCamera camera{};
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
        .writeColor("target", context.renderTarget)
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
        .execute([&frame = context.frame, &bindings = context.bindings,
                  viewTarget = context.viewTarget, camera = context.camera,
                  colorLoadOp = context.colorLoadOp, colorStoreOp = context.colorStoreOp,
                  worldGridPipeline, worldGridPipelineLayout,
                  &eventRecorder = context.eventRecorder](
                     RenderGraphPassContext pass) -> Result<void> {
            return executeBasicRenderViewWorldGridPass(
                frame, pass, bindings, viewTarget.extent, camera, colorLoadOp, colorStoreOp,
                worldGridPipeline, worldGridPipelineLayout, &eventRecorder);
        });
}

void addBasicRenderViewSceneInputsPass(const BasicRenderViewPassRecordingContext& context) {
    context.graph.addPass("RenderViewSceneInputs", kBasicRenderViewSceneInputsPassType)
        .setParams(kBasicRenderViewSceneInputsParamsType, context.policy.sceneInputsParams)
        .hasSideEffects()
        .recordCommands(
            [sceneInputsParams = context.policy.sceneInputsParams](
                RenderGraphCommandList& commands) {
                commands.setInt("SceneDrawItemCount",
                                static_cast<int>(sceneInputsParams.drawItemCount));
            })
        .execute([&eventRecorder = context.eventRecorder](
                     RenderGraphPassContext pass) -> Result<void> {
            return executeBasicRenderViewSceneInputsPass(pass, &eventRecorder);
        });
}

void addBasicRenderViewOverlayPass(const BasicRenderViewPassRecordingContext& context,
                                   VkPipeline debugLinePipeline,
                                   VkBuffer debugLineVertexBuffer,
                                   std::uint32_t debugLineVertexCount) {
    context.graph.addPass("RenderViewOverlayInputs", kBasicRenderViewOverlayPassType)
        .setParams(kBasicRenderViewOverlayParamsType, context.policy.overlayParams)
        .writeColor("target", context.renderTarget)
        .recordCommands(
            [overlayParams = context.policy.overlayParams](RenderGraphCommandList& commands) {
                commands.setShader("Hidden/RenderViewOverlay", "Inputs")
                    .setVec4("CameraPositionNear", overlayParams.cameraPositionNear)
                    .setVec4("FrameTimeScale", overlayParams.frameTimeScale)
                    .setInt("DebugWorldLineCount",
                            static_cast<int>(overlayParams.debugWorldLineCount));
            })
        .execute([&frame = context.frame, &bindings = context.bindings,
                  viewTarget = context.viewTarget, colorLoadOp = context.colorLoadOp,
                  colorStoreOp = context.colorStoreOp, debugLinePipeline, debugLineVertexBuffer,
                  debugLineVertexCount, &eventRecorder = context.eventRecorder](
                     RenderGraphPassContext pass) -> Result<void> {
            return executeBasicRenderViewOverlayPass(
                frame, pass, bindings, viewTarget.extent, colorLoadOp, colorStoreOp,
                debugLinePipeline, debugLineVertexBuffer, debugLineVertexCount, &eventRecorder);
        });
}
