struct TintData {
    float4 color;
};

struct FullscreenVertexOutput {
    float4 position : SV_Position;
};

static const float2 kFullscreenPositions[3] = {
    float2(-1.0, -1.0),
    float2(3.0, -1.0),
    float2(-1.0, 3.0),
};

// VertexID keeps the descriptor smoke independent from mesh buffers.
[shader("vertex")]
FullscreenVertexOutput descriptorVertexMain(uint vertexId : SV_VertexID) {
    FullscreenVertexOutput output;
    output.position = float4(kFullscreenPositions[vertexId], 0.0, 1.0);
    return output;
}

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
