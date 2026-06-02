void addBasicRenderViewWorldGridPass(RenderGraph& graph, RenderGraphImageHandle renderTarget,
                                     const BasicRenderViewPassPolicy& policy,
                                     const VulkanFrameRecordContext& frame,
                                     std::vector<VulkanRenderGraphImageBinding>& bindings,
                                     BasicRenderViewTarget viewTarget, BasicRenderViewCamera camera,
                                     BasicRenderViewOverlayColorLoadOp colorLoadOp,
                                     BasicRenderViewOverlayColorStoreOp colorStoreOp,
                                     VkPipeline worldGridPipeline,
                                     VkPipelineLayout worldGridPipelineLayout,
                                     BasicRenderViewExecutionEventRecorder& eventRecorder) {
    graph.addPass("RenderViewWorldGrid", kBasicRenderViewWorldGridPassType)
        .setParams(kBasicRenderViewWorldGridParamsType, policy.worldGridParams)
        .writeColor("target", renderTarget)
        .recordCommands(
            [worldGridParams = policy.worldGridParams](RenderGraphCommandList& commands) {
                commands.setShader("Hidden/RenderViewWorldGrid", "Fullscreen")
                    .setVec4("CameraPositionNear", worldGridParams.cameraPositionNear)
                    .setVec4("ViewportFade", worldGridParams.viewportFade)
                    .setVec4("GridSettings", worldGridParams.gridSettings)
                    .setVec4("GridLodSettings", worldGridParams.gridLodSettings)
                    .setVec4("GridColor", worldGridParams.gridColor)
                    .drawFullscreenTriangle();
            })
        .execute([&frame, &bindings, viewTarget, camera, colorLoadOp, colorStoreOp,
                  worldGridPipeline, worldGridPipelineLayout,
                  &eventRecorder](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicRenderViewWorldGridPass(
                frame, pass, bindings, viewTarget.extent, camera, colorLoadOp, colorStoreOp,
                worldGridPipeline, worldGridPipelineLayout, &eventRecorder);
        });
}

void addBasicRenderViewSceneInputsPass(RenderGraph& graph, const BasicRenderViewPassPolicy& policy,
                                       BasicRenderViewExecutionEventRecorder& eventRecorder) {
    graph.addPass("RenderViewSceneInputs", kBasicRenderViewSceneInputsPassType)
        .setParams(kBasicRenderViewSceneInputsParamsType, policy.sceneInputsParams)
        .hasSideEffects()
        .recordCommands(
            [sceneInputsParams = policy.sceneInputsParams](RenderGraphCommandList& commands) {
                commands.setInt("SceneDrawItemCount",
                                static_cast<int>(sceneInputsParams.drawItemCount));
            })
        .execute([&eventRecorder](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicRenderViewSceneInputsPass(pass, &eventRecorder);
        });
}

void addBasicRenderViewOverlayPass(
    RenderGraph& graph, RenderGraphImageHandle renderTarget,
    const BasicRenderViewPassPolicy& policy, const VulkanFrameRecordContext& frame,
    std::vector<VulkanRenderGraphImageBinding>& bindings, BasicRenderViewTarget viewTarget,
    BasicRenderViewOverlayColorLoadOp colorLoadOp, BasicRenderViewOverlayColorStoreOp colorStoreOp,
    VkPipeline debugLinePipeline, VkBuffer debugLineVertexBuffer,
    std::uint32_t debugLineVertexCount, BasicRenderViewExecutionEventRecorder& eventRecorder) {
    graph.addPass("RenderViewOverlayInputs", kBasicRenderViewOverlayPassType)
        .setParams(kBasicRenderViewOverlayParamsType, policy.overlayParams)
        .writeColor("target", renderTarget)
        .recordCommands([overlayParams = policy.overlayParams](RenderGraphCommandList& commands) {
            commands.setShader("Hidden/RenderViewOverlay", "Inputs")
                .setVec4("CameraPositionNear", overlayParams.cameraPositionNear)
                .setVec4("FrameTimeScale", overlayParams.frameTimeScale)
                .setInt("DebugWorldLineCount", static_cast<int>(overlayParams.debugWorldLineCount));
        })
        .execute([&frame, &bindings, viewTarget, colorLoadOp, colorStoreOp, debugLinePipeline,
                  debugLineVertexBuffer, debugLineVertexCount,
                  &eventRecorder](RenderGraphPassContext pass) -> Result<void> {
            return executeBasicRenderViewOverlayPass(
                frame, pass, bindings, viewTarget.extent, colorLoadOp, colorStoreOp,
                debugLinePipeline, debugLineVertexBuffer, debugLineVertexCount, &eventRecorder);
        });
}
