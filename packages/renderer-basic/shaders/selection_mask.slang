struct SelectionMaskFragmentInput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("fragment")]
float4 selectionMaskFragmentMain(SelectionMaskFragmentInput input) : SV_Target0 {
    return float4(1.0, 1.0, 1.0, 1.0);
}
