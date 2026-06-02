struct BasicRenderViewPassPolicy {
    bool sceneInputsEnabled{};
    bool worldGridEnabled{};
    bool debugLineOverlayEnabled{};
    BasicRenderViewSceneInputsParams sceneInputsParams{};
    BasicRenderViewWorldGridParams worldGridParams{};
    BasicRenderViewOverlayParams overlayParams{};
};

[[nodiscard]] BasicRenderViewPassPolicy
basicRenderViewPassPolicy(const BasicRenderViewDesc& view,
                          std::span<const BasicDebugWorldLine> debugWorldLines) {
    return BasicRenderViewPassPolicy{
        .sceneInputsEnabled = !view.scene.drawItems.empty(),
        .worldGridEnabled = view.overlay.enabled && view.overlay.worldGrid.enabled,
        .debugLineOverlayEnabled = view.overlay.enabled && !debugWorldLines.empty(),
        .sceneInputsParams = basicRenderViewSceneInputsParams(view),
        .worldGridParams = basicRenderViewWorldGridParams(view),
        .overlayParams = basicRenderViewOverlayParams(view),
    };
}
