struct TintData {
    float4 color;
};

[[vk::binding(0, 0)]]
ConstantBuffer<TintData> gTint;

[[vk::binding(1, 0)]]
Texture2D<float4> gSource;

[[vk::binding(2, 0)]]
SamplerState gSampler;

[shader("fragment")]
float4 descriptorFragmentMain() : SV_Target0 {
    return gTint.color * gSource.Sample(gSampler, float2(0.5, 0.5));
}
