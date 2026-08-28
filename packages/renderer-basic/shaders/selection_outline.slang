struct SelectionOutlineVertexOutput {
    float4 position : SV_Position;
};

[[vk::binding(0, 0)]]
Texture2D<float4> gSelectionMask;

float selectionMaskAt(int2 pixel, int2 maximumPixel) {
    return gSelectionMask.Load(int3(clamp(pixel, int2(0, 0), maximumPixel), 0)).r;
}

[shader("fragment")]
float4 selectionOutlineFragmentMain(SelectionOutlineVertexOutput input) : SV_Target0 {
    uint width;
    uint height;
    gSelectionMask.GetDimensions(width, height);
    const int2 maximumPixel = int2(max(width, 1U) - 1U, max(height, 1U) - 1U);
    const int2 pixel = int2(input.position.xy);
    const float center = selectionMaskAt(pixel, maximumPixel);
    float neighbor = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            if (x != 0 || y != 0) {
                neighbor = max(neighbor, selectionMaskAt(pixel + int2(x, y), maximumPixel));
            }
        }
    }
    const float alpha = saturate(neighbor - center);
    if (alpha <= 0.001) {
        discard;
    }
    return float4(1.0, 0.55, 0.12, alpha);
}
