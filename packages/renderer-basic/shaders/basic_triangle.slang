struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID) {
    VertexOutput output;

    if (vertexId == 0) {
        output.position = float4(0.0, -0.55, 0.0, 1.0);
        output.color = float3(0.95, 0.24, 0.18);
    } else if (vertexId == 1) {
        output.position = float4(0.55, 0.45, 0.0, 1.0);
        output.color = float3(0.12, 0.72, 0.36);
    } else {
        output.position = float4(-0.55, 0.45, 0.0, 1.0);
        output.color = float3(0.18, 0.38, 0.95);
    }

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0 {
    return float4(input.color, 1.0);
}
