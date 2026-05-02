struct TintData {
    float4 color;
};

[[vk::binding(0, 0)]]
ConstantBuffer<TintData> gTint;

[shader("fragment")]
float4 descriptorFragmentMain() : SV_Target0 {
    return gTint.color;
}
