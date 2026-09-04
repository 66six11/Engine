using System;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Viewports;

public sealed class ViewportTranslateGizmoInteraction : IViewportTransformGizmoInteraction
{
    private const float MaximumDragDistanceFactor = 0.95f;
    private const float MinimumPickSegmentPosition = 0.12f;
    private readonly ViewportGizmoMath.Projection projection_;
    private readonly Float3 axisDirection_;
    private readonly Float3 dragPlaneNormal_;
    private readonly float initialAxisParameter_;

    private ViewportTranslateGizmoInteraction(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportExtent extent,
        ViewportGizmoAxis axis,
        ViewportGizmoMath.Projection projection,
        Float3 axisDirection,
        Float3 dragPlaneNormal,
        float initialAxisParameter)
    {
        ObjectId = snapshot.ObjectId;
        ExpectedRevision = snapshot.TargetRevision;
        Extent = extent;
        Axis = axis;
        InitialTransform = snapshot.Transform;
        CurrentTransform = snapshot.Transform;
        projection_ = projection;
        axisDirection_ = axisDirection;
        dragPlaneNormal_ = dragPlaneNormal;
        initialAxisParameter_ = initialAxisParameter;
        MaximumDragDistance = snapshot.Camera.FarPlane * MaximumDragDistanceFactor;
    }

    public Guid ObjectId { get; }

    public ulong ExpectedRevision { get; }

    public ViewportExtent Extent { get; }

    public ViewportGizmoAxis Axis { get; }

    public TransformValue InitialTransform { get; }

    public TransformValue CurrentTransform { get; private set; }

    public bool HasChanged =>
        ViewportGizmoMath.DistanceSquared(InitialTransform.Position, CurrentTransform.Position) >
        1.0e-10f;

    private float MaximumDragDistance { get; }

    public bool TryUpdate(ViewportPickPoint point, out TransformValue transform)
    {
        transform = CurrentTransform;
        if (!ViewportGizmoMath.TryCreateRay(projection_, point, out var ray) ||
            !ViewportGizmoMath.TryIntersectPlane(
                ray,
                InitialTransform.Position,
                dragPlaneNormal_,
                out var pointOnPlane))
        {
            return false;
        }

        var axisParameter = ViewportGizmoMath.Dot(
            ViewportGizmoMath.Subtract(pointOnPlane, InitialTransform.Position),
            axisDirection_);
        var delta = Math.Clamp(
            axisParameter - initialAxisParameter_,
            -MaximumDragDistance,
            MaximumDragDistance);
        var position = ViewportGizmoMath.Add(
            InitialTransform.Position,
            ViewportGizmoMath.Scale(axisDirection_, delta));
        if (!ViewportGizmoMath.IsFinite(position))
        {
            return false;
        }

        CurrentTransform = new TransformValue(
            position,
            InitialTransform.Rotation,
            InitialTransform.Scale);
        transform = CurrentTransform;
        return true;
    }

    internal static bool TryCreate(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out ViewportTranslateGizmoInteraction interaction)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        interaction = null!;
        if (snapshot.Kind != ViewportTransformGizmoKind.Translate ||
            !ViewportGizmoMath.TryCreateProjection(
                snapshot.Camera,
                request.Extent,
                out var projection) ||
            !TryHitTest(projection, snapshot.Transform.Position, request, out var axis))
        {
            return false;
        }

        var axisDirection = ViewportGizmoMath.AxisDirection(axis);
        var dragPlaneNormal = ViewportGizmoMath.Normalize(ViewportGizmoMath.Subtract(
            projection.Forward,
            ViewportGizmoMath.Scale(
                axisDirection,
                ViewportGizmoMath.Dot(projection.Forward, axisDirection))));
        if (!ViewportGizmoMath.IsFinite(dragPlaneNormal) ||
            !ViewportGizmoMath.TryCreateRay(projection, request.Point, out var ray) ||
            !ViewportGizmoMath.TryIntersectPlane(
                ray,
                snapshot.Transform.Position,
                dragPlaneNormal,
                out var pointOnPlane))
        {
            return false;
        }

        var initialAxisParameter = ViewportGizmoMath.Dot(
            ViewportGizmoMath.Subtract(pointOnPlane, snapshot.Transform.Position),
            axisDirection);
        interaction = new ViewportTranslateGizmoInteraction(
            snapshot,
            request.Extent,
            axis,
            projection,
            axisDirection,
            dragPlaneNormal,
            initialAxisParameter);
        return true;
    }

    private static bool TryHitTest(
        ViewportGizmoMath.Projection projection,
        Float3 origin,
        ViewportPickRequest request,
        out ViewportGizmoAxis axis)
    {
        axis = ViewportGizmoAxis.None;
        if (request.Point.X < 0 || request.Point.Y < 0 ||
            request.Point.X >= request.Extent.Width ||
            request.Point.Y >= request.Extent.Height ||
            !ViewportGizmoMath.TryWorldLength(projection, origin, out var worldLength) ||
            !ViewportGizmoMath.TryProject(projection, origin, out var start))
        {
            return false;
        }

        var bestDistance = float.PositiveInfinity;
        foreach (var candidateAxis in new[]
                 {
                     ViewportGizmoAxis.X,
                     ViewportGizmoAxis.Y,
                     ViewportGizmoAxis.Z,
                 })
        {
            var endpoint = ViewportGizmoMath.Add(
                origin,
                ViewportGizmoMath.Scale(
                    ViewportGizmoMath.AxisDirection(candidateAxis),
                    worldLength));
            if (!ViewportGizmoMath.TryProject(projection, endpoint, out var end))
            {
                continue;
            }

            var segmentX = end.X - start.X;
            var segmentY = end.Y - start.Y;
            if (segmentX * segmentX + segmentY * segmentY <= ViewportGizmoMath.Epsilon)
            {
                continue;
            }

            var distance = ViewportGizmoMath.DistanceToSegment(
                request.Point,
                start,
                end,
                out var segmentPosition);
            if (segmentPosition < MinimumPickSegmentPosition ||
                distance > request.TolerancePixels || distance >= bestDistance)
            {
                continue;
            }

            bestDistance = distance;
            axis = candidateAxis;
        }

        return axis != ViewportGizmoAxis.None;
    }

    internal static ViewportGizmoAxis HitTest(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        return snapshot.Kind == ViewportTransformGizmoKind.Translate &&
            ViewportGizmoMath.TryCreateProjection(
                snapshot.Camera,
                request.Extent,
                out var projection) &&
            TryHitTest(projection, snapshot.Transform.Position, request, out var axis)
                ? axis
                : ViewportGizmoAxis.None;
    }
}

public static class ViewportTranslateGizmoManipulator
{
    public static ViewportGizmoAxis HitTest(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request) =>
        ViewportTranslateGizmoInteraction.HitTest(snapshot, request);

    public static bool TryBegin(
        ViewportTransformGizmoSnapshot snapshot,
        ViewportPickRequest request,
        out ViewportTranslateGizmoInteraction interaction) =>
        ViewportTranslateGizmoInteraction.TryCreate(snapshot, request, out interaction);
}
