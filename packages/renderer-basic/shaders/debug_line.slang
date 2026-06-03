struct DebugLineVertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
DebugLineVertexOutput debugLineVertexMain(float2 position : POSITION0, float4 color : COLOR0) {
    DebugLineVertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.color = color;
    return output;
}

[shader("fragment")]
float4 debugLineFragmentMain(DebugLineVertexOutput input) : SV_Target0 {
    return input.color;
}
