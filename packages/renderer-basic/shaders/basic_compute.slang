[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> gValues;

[numthreads(1, 1, 1)]
[shader("compute")]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    const uint index = dispatchThreadId.x;
    gValues[index] = 0xA5000000u | index;
}
