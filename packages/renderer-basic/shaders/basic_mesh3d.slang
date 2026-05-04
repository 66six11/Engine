struct Mesh3DPushConstants {
    float4 mvpRow0;
    float4 mvpRow1;
    float4 mvpRow2;
    float4 mvpRow3;
};

[[vk::push_constant]]
ConstantBuffer<Mesh3DPushConstants> gMesh3D;

struct Mesh3DVertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
Mesh3DVertexOutput mesh3DVertexMain(float3 position : POSITION0, float3 color : COLOR0) {
    Mesh3DVertexOutput output;
    const float4 localPosition = float4(position, 1.0);
    output.position = float4(
        dot(gMesh3D.mvpRow0, localPosition),
        dot(gMesh3D.mvpRow1, localPosition),
        dot(gMesh3D.mvpRow2, localPosition),
        dot(gMesh3D.mvpRow3, localPosition));
    output.color = color;
    return output;
}

[shader("fragment")]
float4 mesh3DFragmentMain(Mesh3DVertexOutput input) : SV_Target0 {
    return float4(input.color, 1.0);
}
