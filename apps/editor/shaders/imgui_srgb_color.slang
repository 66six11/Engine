struct FragmentInput {
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
Sampler2D gTexture;

float srgbChannelToLinear(float value) {
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return pow((value + 0.055) / 1.055, 2.4);
}

float3 srgbToLinear(float3 color) {
    return float3(
        srgbChannelToLinear(color.r),
        srgbChannelToLinear(color.g),
        srgbChannelToLinear(color.b));
}

[shader("fragment")]
float4 imguiFragmentMain(FragmentInput input) : SV_Target0 {
    const float4 texel = gTexture.Sample(input.uv);

    // ImGui vertex colors carry sRGB-encoded authoring colors. Texture RGB is
    // already in the pass working space through its image view format contract.
    const float3 vertexRgbLinear = srgbToLinear(input.color.rgb);
    const float3 outputRgb = vertexRgbLinear * texel.rgb;
    const float outputAlpha = input.color.a * texel.a;
    return float4(outputRgb, outputAlpha);
}
