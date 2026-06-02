struct BasicRenderViewPassPolicy {
    bool worldGridEnabled{};
    bool debugLineOverlayEnabled{};
    BasicRenderViewWorldGridParams worldGridParams{};
    BasicRenderViewOverlayParams overlayParams{};
};

[[nodiscard]] BasicRenderViewPassPolicy
basicRenderViewPassPolicy(const BasicRenderViewDesc& view,
                          std::span<const BasicDebugWorldLine> debugWorldLines) {
    return BasicRenderViewPassPolicy{
        .worldGridEnabled = view.overlay.enabled && view.overlay.worldGrid.enabled,
        .debugLineOverlayEnabled = view.overlay.enabled && !debugWorldLines.empty(),
        .worldGridParams = basicRenderViewWorldGridParams(view),
        .overlayParams = basicRenderViewOverlayParams(view),
    };
}
