using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Viewports;

public readonly record struct ViewportSessionId
{
    public ViewportSessionId(Guid value)
    {
        if (value == Guid.Empty)
        {
            throw new ArgumentException("Viewport session id must not be empty.", nameof(value));
        }

        Value = value;
    }

    public Guid Value { get; }

    public bool IsValid => Value != Guid.Empty;

    public static ViewportSessionId Create() => new(Guid.NewGuid());
}

public enum ViewportRenderKind : uint
{
    Scene = 0,
    Game = 1,
    Preview = 2,
}

public enum ViewportTargetKind : uint
{
    DocumentScene = 0,
}

[Flags]
public enum ViewportInvalidationReason : uint
{
    None = 0,
    InitialFrame = 1U << 0,
    TargetChanged = 1U << 1,
    CameraChanged = 1U << 2,
    ExtentChanged = 1U << 3,
    Exposed = 1U << 4,
    Realtime = 1U << 5,
}

public readonly record struct ViewportExtent
{
    public ViewportExtent(uint width, uint height)
    {
        if (width == 0 || height == 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(width),
                "Viewport extent dimensions must be non-zero.");
        }

        Width = width;
        Height = height;
    }

    public uint Width { get; }

    public uint Height { get; }

    internal bool IsRenderable => Width != 0 && Height != 0;
}

public readonly record struct ViewportRenderSize
{
    public ViewportRenderSize(ViewportExtent logicalExtent, ViewportExtent allocationExtent)
    {
        if (!logicalExtent.IsRenderable)
        {
            throw new ArgumentOutOfRangeException(nameof(logicalExtent));
        }
        if (!allocationExtent.IsRenderable ||
            allocationExtent.Width < logicalExtent.Width ||
            allocationExtent.Height < logicalExtent.Height)
        {
            throw new ArgumentOutOfRangeException(
                nameof(allocationExtent),
                "Viewport allocation extent must contain the logical extent.");
        }

        LogicalExtent = logicalExtent;
        AllocationExtent = allocationExtent;
    }

    public ViewportExtent LogicalExtent { get; }

    public ViewportExtent AllocationExtent { get; }
}

public sealed record ViewportCameraSnapshot
{
    public ViewportCameraSnapshot(
        Float3 position,
        Float3 target,
        Float3 up,
        float verticalFovRadians,
        float nearPlane,
        float farPlane)
    {
        if (!IsFinite(position) || !IsFinite(target) || !IsFinite(up) ||
            !float.IsFinite(verticalFovRadians) || !float.IsFinite(nearPlane) ||
            !float.IsFinite(farPlane) || verticalFovRadians <= 0 ||
            verticalFovRadians >= MathF.PI || nearPlane <= 0 || farPlane <= nearPlane ||
            LengthSquared(position, target) <= 1.0e-8f ||
            LengthSquared(up, Float3.Zero) <= 1.0e-8f)
        {
            throw new ArgumentException("Viewport camera values are invalid.");
        }

        Position = position;
        Target = target;
        Up = up;
        VerticalFovRadians = verticalFovRadians;
        NearPlane = nearPlane;
        FarPlane = farPlane;
    }

    public Float3 Position { get; }

    public Float3 Target { get; }

    public Float3 Up { get; }

    public float VerticalFovRadians { get; }

    public float NearPlane { get; }

    public float FarPlane { get; }

    public static ViewportCameraSnapshot DefaultScene { get; } = new(
        new Float3(0, 2, -6),
        Float3.Zero,
        new Float3(0, 1, 0),
        MathF.PI / 3,
        0.1f,
        1000.0f);

    private static bool IsFinite(Float3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private static float LengthSquared(Float3 lhs, Float3 rhs)
    {
        var x = lhs.X - rhs.X;
        var y = lhs.Y - rhs.Y;
        var z = lhs.Z - rhs.Z;
        return x * x + y * y + z * z;
    }
}

public sealed record ViewportDebugProxySnapshot
{
    public ViewportDebugProxySnapshot(Guid objectId, TransformValue transform)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Debug proxy object id must not be empty.", nameof(objectId));
        }

        ObjectId = objectId;
        Transform = transform;
    }

    public Guid ObjectId { get; }

    public TransformValue Transform { get; }
}

public sealed record ViewportRenderRequest
{
    public const int MaximumDebugProxyCount = 256;

    internal ViewportRenderRequest(
        ViewportSessionId sessionId,
        ulong sequence,
        ViewportRenderKind kind,
        ViewportTargetKind targetKind,
        Guid targetId,
        ulong targetRevision,
        ViewportRenderSize renderSize,
        ViewportCameraSnapshot camera,
        ViewportInvalidationReason reasons,
        IEnumerable<ViewportDebugProxySnapshot> debugProxies,
        int totalDebugProxyCount)
    {
        SessionId = sessionId;
        Sequence = sequence;
        Kind = kind;
        TargetKind = targetKind;
        TargetId = targetId;
        TargetRevision = targetRevision;
        RenderSize = renderSize;
        LogicalExtent = renderSize.LogicalExtent;
        AllocationExtent = renderSize.AllocationExtent;
        Camera = camera;
        Reasons = reasons;
        DebugProxies = new ReadOnlyCollection<ViewportDebugProxySnapshot>(debugProxies.ToArray());
        TotalDebugProxyCount = totalDebugProxyCount;
    }

    public ViewportSessionId SessionId { get; }

    public ulong Sequence { get; }

    public ViewportRenderKind Kind { get; }

    public ViewportTargetKind TargetKind { get; }

    public Guid TargetId { get; }

    public ulong TargetRevision { get; }

    public ViewportRenderSize RenderSize { get; }

    public ViewportExtent LogicalExtent { get; }

    public ViewportExtent AllocationExtent { get; }

    public ViewportCameraSnapshot Camera { get; }

    public ViewportInvalidationReason Reasons { get; }

    public IReadOnlyList<ViewportDebugProxySnapshot> DebugProxies { get; }

    public int TotalDebugProxyCount { get; }

    public bool DebugProxiesTruncated => DebugProxies.Count < TotalDebugProxyCount;
}

public sealed record ViewportSessionSnapshot(
    ViewportSessionId SessionId,
    ViewportRenderKind Kind,
    ViewportTargetKind TargetKind,
    Guid TargetId,
    ulong TargetRevision,
    ulong LastSequence,
    ulong MinimumPresentableSequence,
    bool IsFrameInFlight,
    ViewportInvalidationReason PendingReasons,
    bool IsClosed);
