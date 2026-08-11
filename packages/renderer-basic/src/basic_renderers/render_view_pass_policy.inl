struct BasicRenderViewPassPolicy {
    bool sceneMeshEnabled{};
    bool worldGridEnabled{};
    bool debugLineOverlayEnabled{};
    BasicRenderViewSceneMeshParams sceneMeshParams{};
    BasicRenderViewWorldGridParams worldGridParams{};
    BasicRenderViewOverlayParams overlayParams{};
};

[[nodiscard]] Result<BasicRenderViewPassPolicy>
basicRenderViewPassPolicy(const BasicRenderViewDesc& view,
                          std::span<const BasicDebugWorldLine> debugWorldLines) {
    auto sceneMeshParams = basicRenderViewSceneMeshParams(view);
    if (!sceneMeshParams) {
        return std::unexpected{std::move(sceneMeshParams.error())};
    }
    return BasicRenderViewPassPolicy{
        .sceneMeshEnabled = !view.scene.drawItems.empty(),
        .worldGridEnabled = view.overlay.enabled && view.overlay.worldGrid.enabled,
        .debugLineOverlayEnabled = view.overlay.enabled && !debugWorldLines.empty(),
        .sceneMeshParams = *sceneMeshParams,
        .worldGridParams = basicRenderViewWorldGridParams(view),
        .overlayParams = basicRenderViewOverlayParams(view),
    };
}
