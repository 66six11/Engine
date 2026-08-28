struct SelectionOutlineVertexOutput {
    float4 position : SV_Position;
};

static const float2 kSelectionOutlineFullscreenPositions[3] = {
    float2(-1.0, -1.0),
    float2(3.0, -1.0),
    float2(-1.0, 3.0),
};

[shader("vertex")]
SelectionOutlineVertexOutput selectionOutlineVertexMain(uint vertexId : SV_VertexID) {
    SelectionOutlineVertexOutput output;
    output.position = float4(kSelectionOutlineFullscreenPositions[vertexId], 0.0, 1.0);
    return output;
}
