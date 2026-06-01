struct WorldGridPushConstants {
    float4 inverseViewProjectionRow0;
    float4 inverseViewProjectionRow1;
    float4 inverseViewProjectionRow2;
    float4 inverseViewProjectionRow3;
    float4 cameraPositionNear;
    float4 viewportFade;
    float4 gridSettings;
};

[[vk::push_constant]]
ConstantBuffer<WorldGridPushConstants> gWorldGrid;

struct WorldGridVertexOutput {
    float4 position : SV_Position;
};

static const float2 kWorldGridFullscreenPositions[3] = {
    float2(-1.0, -1.0),
    float2(3.0, -1.0),
    float2(-1.0, 3.0),
};

[shader("vertex")]
WorldGridVertexOutput worldGridVertexMain(uint vertexId : SV_VertexID) {
    WorldGridVertexOutput output;
    output.position = float4(kWorldGridFullscreenPositions[vertexId], 0.0, 1.0);
    return output;
}

float4 multiplyInverseViewProjection(float4 clipPosition) {
    return float4(
        dot(gWorldGrid.inverseViewProjectionRow0, clipPosition),
        dot(gWorldGrid.inverseViewProjectionRow1, clipPosition),
        dot(gWorldGrid.inverseViewProjectionRow2, clipPosition),
        dot(gWorldGrid.inverseViewProjectionRow3, clipPosition));
}

float3 unprojectWorldGridPoint(float2 pixelPosition, float clipZ) {
    const float2 viewportSize = max(gWorldGrid.viewportFade.xy, float2(1.0, 1.0));
    const float2 ndc = float2(
        ((pixelPosition.x / viewportSize.x) * 2.0) - 1.0,
        1.0 - ((pixelPosition.y / viewportSize.y) * 2.0));
    const float4 world = multiplyInverseViewProjection(float4(ndc, clipZ, 1.0));
    const float safeW = abs(world.w) <= 0.000001
        ? (world.w < 0.0 ? -0.000001 : 0.000001)
        : world.w;
    return world.xyz / safeW;
}

float gridLineAlpha(float2 worldCoord, float spacing) {
    const float2 scaled = worldCoord / max(spacing, 0.0001);
    const float2 derivative = max(fwidth(scaled), float2(0.0001, 0.0001));
    const float2 centered = abs(frac(scaled - 0.5) - 0.5) / derivative;
    return 1.0 - saturate(min(centered.x, centered.y));
}

float axisLineAlpha(float value) {
    const float derivative = max(fwidth(value), 0.0001);
    return 1.0 - saturate(abs(value) / derivative);
}

[shader("fragment")]
float4 worldGridFragmentMain(WorldGridVertexOutput input) : SV_Target0 {
    const float3 nearWorld = unprojectWorldGridPoint(input.position.xy, 0.0);
    const float3 farWorld = unprojectWorldGridPoint(input.position.xy, 1.0);
    const float3 ray = farWorld - nearWorld;
    if (abs(ray.y) <= 0.000001) {
        discard;
    }

    const float planeY = gWorldGrid.gridSettings.z;
    const float rayT = (planeY - nearWorld.y) / ray.y;
    if (rayT < 0.0) {
        discard;
    }

    const float3 worldPosition = nearWorld + (ray * rayT);
    const float2 gridCoord = worldPosition.xz;
    const float minorSpacing = max(gWorldGrid.gridSettings.x, 0.0001);
    const float majorSpacing = max(gWorldGrid.gridSettings.y, minorSpacing);
    const float opacity = saturate(gWorldGrid.gridSettings.w);

    const float minorAlpha = gridLineAlpha(gridCoord, minorSpacing);
    const float majorAlpha = gridLineAlpha(gridCoord, majorSpacing);
    const float xAxisAlpha = axisLineAlpha(worldPosition.z);
    const float zAxisAlpha = axisLineAlpha(worldPosition.x);

    const float3 minorColor = float3(0.22, 0.25, 0.30);
    const float3 majorColor = float3(0.36, 0.39, 0.44);
    const float3 xAxisColor = float3(0.84, 0.22, 0.18);
    const float3 zAxisColor = float3(0.22, 0.42, 0.92);

    float alpha = minorAlpha * 0.48;
    float3 color = minorColor;
    if (majorAlpha > alpha) {
        alpha = majorAlpha * 0.68;
        color = majorColor;
    }
    if (xAxisAlpha > alpha) {
        alpha = xAxisAlpha;
        color = xAxisColor;
    }
    if (zAxisAlpha > alpha) {
        alpha = zAxisAlpha;
        color = zAxisColor;
    }

    const float distanceToCamera =
        length(worldPosition - gWorldGrid.cameraPositionNear.xyz);
    const float fadeStart = gWorldGrid.viewportFade.z;
    const float fadeEnd = max(gWorldGrid.viewportFade.w, fadeStart + 0.0001);
    const float distanceFade =
        1.0 - saturate((distanceToCamera - fadeStart) / (fadeEnd - fadeStart));
    alpha *= opacity * distanceFade;
    if (alpha <= 0.001) {
        discard;
    }

    return float4(color, alpha);
}
