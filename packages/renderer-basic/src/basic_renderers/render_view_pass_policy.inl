struct BasicRenderViewPassPolicy {
    bool sceneMeshEnabled{};
    bool worldGridEnabled{};
    bool debugLineOverlayEnabled{};
    bool selectionOutlineEnabled{};
    BasicRenderViewSceneMeshParams sceneMeshParams{};
    BasicRenderViewWorldGridParams worldGridParams{};
    BasicRenderViewOverlayParams overlayParams{};
    BasicRenderViewSelectionMaskParams selectionMaskParams{};
    BasicRenderViewSelectionOutlineParams selectionOutlineParams{};
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
        .selectionOutlineEnabled =
            view.overlay.enabled && !view.overlay.selectionOutline.drawItems.empty(),
        .sceneMeshParams = *sceneMeshParams,
        .worldGridParams = basicRenderViewWorldGridParams(view),
        .overlayParams = basicRenderViewOverlayParams(view),
        .selectionMaskParams = basicRenderViewSelectionMaskParams(view),
        .selectionOutlineParams = basicRenderViewSelectionOutlineParams(view),
    };
}
