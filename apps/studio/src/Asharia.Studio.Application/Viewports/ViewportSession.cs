using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;

namespace Asharia.Studio.Application.Viewports;

public sealed class ViewportSession
{
    private static readonly ViewportLocalBounds DirectionalWedgeValidationLocalBounds = new(
        new Float3(-1.25f, -0.125f, -0.625f),
        new Float3(1.5f, 0.25f, 0.75f));

    private readonly object gate_ = new();
    private readonly ViewportSessionId sessionId_;
    private readonly ViewportRenderKind kind_;
    private readonly ViewportTargetKind targetKind_ = ViewportTargetKind.DocumentScene;
    private Guid targetId_;
    private ulong targetRevision_;
    private ViewportCameraSnapshot camera_;
    private ViewportDebugProxySnapshot[] debugProxies_;
    private int totalDebugProxyCount_;
    private ViewportModelPickProxySnapshot[] modelPickProxies_;
    private ViewportAuthoredMeshSnapshot[] authoredMeshes_;
    private Dictionary<Guid, TransformValue> entityTransforms_;
    private ViewportSceneRasterMode sceneRasterMode_ = ViewportSceneRasterMode.Solid;
    private ulong viewStateRevision_;
    private Guid? selectedObjectId_;
    private ViewportTranslateGizmoState? translateGizmo_;
    private ViewportRenderSize? lastRenderSize_;
    private ViewportInvalidationReason pendingReasons_ = ViewportInvalidationReason.InitialFrame;
    private ulong lastSequence_;
    private ulong minimumPresentableSequence_ = 1;
    private ulong minimumInteractiveCameraSequence_;
    private ulong inFlightSequence_;
    private ulong inFlightTargetRevision_;
    private ViewportInvalidationReason inFlightReasons_;
    private bool isClosed_;

    public event EventHandler? RefreshRequested;

    public ViewportSession(
        ViewportSessionId sessionId,
        ViewportRenderKind kind,
        SceneDocumentSnapshot document,
        ViewportCameraSnapshot camera)
    {
        if (!sessionId.IsValid)
        {
            throw new ArgumentException("Viewport session id must be valid.", nameof(sessionId));
        }
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(camera);

        sessionId_ = sessionId;
        kind_ = kind;
        targetId_ = document.SceneId;
        targetRevision_ = document.Revision;
        camera_ = camera;
        (debugProxies_, totalDebugProxyCount_) = CaptureDebugProxies(document);
        modelPickProxies_ = CaptureModelPickProxies(document);
        authoredMeshes_ = CaptureAuthoredMeshes(document);
        entityTransforms_ = CaptureEntityTransforms(document);
    }

    public ViewportSessionSnapshot Current
    {
        get
        {
            lock (gate_)
            {
                return SnapshotLocked();
            }
        }
    }

    public ViewportCameraSnapshot Camera
    {
        get
        {
            lock (gate_)
            {
                return camera_;
            }
        }
    }

    public bool TryCapturePickSnapshot(
        ViewportSessionId expectedSessionId,
        Guid expectedTargetId,
        ulong expectedTargetRevision,
        out ViewportPickSnapshot snapshot)
    {
        lock (gate_)
        {
            snapshot = null!;
            if (isClosed_ || kind_ != ViewportRenderKind.Scene ||
                expectedSessionId != sessionId_ || expectedTargetId != targetId_ ||
                expectedTargetRevision != targetRevision_)
            {
                return false;
            }

            snapshot = new ViewportPickSnapshot(
                sessionId_,
                targetId_,
                targetRevision_,
                camera_,
                modelPickProxies_,
                debugProxies_,
                totalDebugProxyCount_);
            return true;
        }
    }

    public bool TryCaptureTranslateGizmoSnapshot(
        ViewportSessionId expectedSessionId,
        Guid expectedTargetId,
        ulong expectedTargetRevision,
        ulong expectedPresentedSequence,
        out ViewportTranslateGizmoSnapshot snapshot)
    {
        lock (gate_)
        {
            snapshot = null!;
            if (isClosed_ || kind_ != ViewportRenderKind.Scene ||
                expectedSessionId != sessionId_ || expectedTargetId != targetId_ ||
                expectedTargetRevision != targetRevision_ ||
                !CanUsePublishedFrameForInteractionLocked(
                    expectedPresentedSequence,
                    expectedTargetRevision) ||
                translateGizmo_ is not { } gizmo)
            {
                return false;
            }

            snapshot = new ViewportTranslateGizmoSnapshot(
                gizmo.ObjectId,
                targetRevision_,
                camera_,
                gizmo.Transform);
            return true;
        }
    }

    public bool TryCapturePickSnapshot(
        ViewportSessionId expectedSessionId,
        Guid expectedTargetId,
        ulong expectedTargetRevision,
        ulong expectedPresentedSequence,
        out ViewportPickSnapshot snapshot)
    {
        lock (gate_)
        {
            snapshot = null!;
            if (isClosed_ || kind_ != ViewportRenderKind.Scene ||
                expectedSessionId != sessionId_ || expectedTargetId != targetId_ ||
                expectedTargetRevision != targetRevision_ ||
                !CanUsePublishedFrameForInteractionLocked(
                    expectedPresentedSequence,
                    expectedTargetRevision))
            {
                return false;
            }

            snapshot = new ViewportPickSnapshot(
                sessionId_,
                targetId_,
                targetRevision_,
                camera_,
                modelPickProxies_,
                debugProxies_,
                totalDebugProxyCount_);
            return true;
        }
    }

    public void SynchronizeDocument(SceneDocumentSnapshot document)
    {
        ArgumentNullException.ThrowIfNull(document);
        var requestRefresh = false;
        lock (gate_)
        {
            ThrowIfClosed();
            if (document.SceneId != targetId_)
            {
                throw new ArgumentException(
                    "A viewport session cannot change its document target in place.",
                    nameof(document));
            }
            if (document.Revision == targetRevision_)
            {
                return;
            }
            if (document.Revision < targetRevision_)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(document),
                    "Viewport document revisions must advance monotonically.");
            }

            var (nextDebugProxies, nextTotalDebugProxyCount) = CaptureDebugProxies(document);
            var nextModelPickProxies = CaptureModelPickProxies(document);
            var nextAuthoredMeshes = CaptureAuthoredMeshes(document);
            var nextEntityTransforms = CaptureEntityTransforms(document);
            targetRevision_ = document.Revision;
            debugProxies_ = nextDebugProxies;
            totalDebugProxyCount_ = nextTotalDebugProxyCount;
            modelPickProxies_ = nextModelPickProxies;
            authoredMeshes_ = nextAuthoredMeshes;
            entityTransforms_ = nextEntityTransforms;
            translateGizmo_ = CreateTranslateGizmoLocked(selectedObjectId_);
            requestRefresh = InvalidateLocked(
                ViewportInvalidationReason.TargetChanged,
                advancePresentationFence: true);
        }
        RaiseRefreshRequested(requestRefresh);
    }

    public void SetCamera(ViewportCameraSnapshot camera)
    {
        ArgumentNullException.ThrowIfNull(camera);
        var requestRefresh = false;
        lock (gate_)
        {
            ThrowIfClosed();
            if (camera_ == camera)
            {
                return;
            }

            camera_ = camera;
            minimumInteractiveCameraSequence_ = 0;
            requestRefresh = InvalidateLocked(
                ViewportInvalidationReason.CameraChanged,
                advancePresentationFence: false);
        }
        RaiseRefreshRequested(requestRefresh);
    }

    public void SetSceneRasterMode(ViewportSceneRasterMode rasterMode)
    {
        if (!Enum.IsDefined(rasterMode))
        {
            throw new ArgumentOutOfRangeException(nameof(rasterMode), rasterMode, null);
        }

        var requestRefresh = false;
        lock (gate_)
        {
            ThrowIfClosed();
            if (sceneRasterMode_ == rasterMode)
            {
                return;
            }
            sceneRasterMode_ = rasterMode;
            requestRefresh = InvalidateLocked(
                ViewportInvalidationReason.TargetChanged,
                advancePresentationFence: true);
        }
        RaiseRefreshRequested(requestRefresh);
    }

    public void SetSelection(ulong viewStateRevision, Guid? selectedObjectId)
    {
        if (selectedObjectId == Guid.Empty)
        {
            throw new ArgumentException(
                "A selected viewport object id must be non-empty.",
                nameof(selectedObjectId));
        }
        if (kind_ != ViewportRenderKind.Scene && selectedObjectId is not null)
        {
            throw new InvalidOperationException(
                "Only Scene viewports can carry an editor object selection.");
        }

        var requestRefresh = false;
        lock (gate_)
        {
            ThrowIfClosed();
            if (viewStateRevision < viewStateRevision_)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(viewStateRevision),
                    "Viewport view-state revisions must advance monotonically.");
            }
            if (viewStateRevision == viewStateRevision_)
            {
                if (selectedObjectId_ != selectedObjectId)
                {
                    throw new ArgumentException(
                        "A viewport view-state revision cannot identify two selections.",
                        nameof(selectedObjectId));
                }
                return;
            }

            viewStateRevision_ = viewStateRevision;
            selectedObjectId_ = selectedObjectId;
            translateGizmo_ = CreateTranslateGizmoLocked(selectedObjectId);
            requestRefresh = InvalidateLocked(
                ViewportInvalidationReason.SelectionChanged,
                advancePresentationFence: true);
        }
        RaiseRefreshRequested(requestRefresh);
    }

    public void SetTranslateGizmo(ViewportTranslateGizmoState gizmo)
    {
        ArgumentNullException.ThrowIfNull(gizmo);
        var requestRefresh = false;
        lock (gate_)
        {
            ThrowIfClosed();
            if (kind_ != ViewportRenderKind.Scene ||
                selectedObjectId_ != gizmo.ObjectId ||
                !entityTransforms_.ContainsKey(gizmo.ObjectId))
            {
                throw new InvalidOperationException(
                    "Translate Gizmo state must target the current Scene selection.");
            }
            if (translateGizmo_ == gizmo)
            {
                return;
            }

            translateGizmo_ = gizmo;
            requestRefresh = InvalidateLocked(
                ViewportInvalidationReason.GizmoChanged,
                advancePresentationFence: false);
        }
        RaiseRefreshRequested(requestRefresh);
    }

    public void ResetTranslateGizmo()
    {
        var requestRefresh = false;
        lock (gate_)
        {
            ThrowIfClosed();
            var reset = CreateTranslateGizmoLocked(selectedObjectId_);
            if (translateGizmo_ == reset)
            {
                return;
            }

            translateGizmo_ = reset;
            requestRefresh = InvalidateLocked(
                ViewportInvalidationReason.GizmoChanged,
                advancePresentationFence: false);
        }
        RaiseRefreshRequested(requestRefresh);
    }

    public void Invalidate(ViewportInvalidationReason reasons)
    {
        if (reasons == ViewportInvalidationReason.None ||
            (reasons & ~AllInvalidationReasons) != ViewportInvalidationReason.None)
        {
            throw new ArgumentOutOfRangeException(nameof(reasons), reasons, null);
        }

        var requestRefresh = false;
        lock (gate_)
        {
            ThrowIfClosed();
            if (reasons.HasFlag(ViewportInvalidationReason.CameraChanged))
            {
                minimumInteractiveCameraSequence_ = 0;
            }
            requestRefresh = InvalidateLocked(
                reasons,
                advancePresentationFence:
                    (reasons & PresentationInvalidationReasons) !=
                    ViewportInvalidationReason.None);
        }
        RaiseRefreshRequested(requestRefresh);
    }

    public bool TryBeginRender(ViewportRenderSize renderSize, out ViewportRenderRequest request)
    {
        if (!renderSize.LogicalExtent.IsRenderable || !renderSize.AllocationExtent.IsRenderable)
        {
            throw new ArgumentOutOfRangeException(nameof(renderSize));
        }

        lock (gate_)
        {
            request = null!;
            if (isClosed_ || inFlightSequence_ != 0)
            {
                return false;
            }
            if (lastRenderSize_ != renderSize)
            {
                lastRenderSize_ = renderSize;
                _ = InvalidateLocked(
                    ViewportInvalidationReason.ExtentChanged,
                    advancePresentationFence: false);
            }
            if (pendingReasons_ == ViewportInvalidationReason.None)
            {
                return false;
            }

            var sequence = checked(++lastSequence_);
            MarkCameraPublishedLocked(sequence);
            var reasons = pendingReasons_;
            pendingReasons_ = ViewportInvalidationReason.None;
            inFlightSequence_ = sequence;
            inFlightTargetRevision_ = targetRevision_;
            inFlightReasons_ = reasons;
            request = new ViewportRenderRequest(
                sessionId_,
                sequence,
                kind_,
                targetKind_,
                targetId_,
                targetRevision_,
                renderSize,
                camera_,
                reasons,
                debugProxies_,
                totalDebugProxyCount_,
                authoredMeshes_,
                sceneRasterMode_,
                viewStateRevision_,
                selectedObjectId_,
                translateGizmo_);
            return true;
        }
    }

    public bool TryPublishLatest(ViewportRenderSize renderSize, out ViewportRenderRequest request)
    {
        if (!renderSize.LogicalExtent.IsRenderable || !renderSize.AllocationExtent.IsRenderable)
        {
            throw new ArgumentOutOfRangeException(nameof(renderSize));
        }

        lock (gate_)
        {
            request = null!;
            if (isClosed_)
            {
                return false;
            }
            if (lastRenderSize_ != renderSize)
            {
                lastRenderSize_ = renderSize;
                _ = InvalidateLocked(
                    ViewportInvalidationReason.ExtentChanged,
                    advancePresentationFence: false);
            }
            if (pendingReasons_ == ViewportInvalidationReason.None)
            {
                return false;
            }

            var sequence = checked(++lastSequence_);
            MarkCameraPublishedLocked(sequence);
            var reasons = pendingReasons_;
            pendingReasons_ = ViewportInvalidationReason.None;
            request = new ViewportRenderRequest(
                sessionId_,
                sequence,
                kind_,
                targetKind_,
                targetId_,
                targetRevision_,
                renderSize,
                camera_,
                reasons,
                debugProxies_,
                totalDebugProxyCount_,
                authoredMeshes_,
                sceneRasterMode_,
                viewStateRevision_,
                selectedObjectId_,
                translateGizmo_);
            return true;
        }
    }

    public bool CanPresentPublishedFrame(ulong sequence, ulong targetRevision)
    {
        lock (gate_)
        {
            return CanPresentPublishedFrameLocked(sequence, targetRevision);
        }
    }

    public bool CanPresentPublishedFrame(
        ulong sequence,
        ulong targetRevision,
        ulong viewStateRevision)
    {
        lock (gate_)
        {
            return CanPresentPublishedFrameLocked(sequence, targetRevision) &&
                viewStateRevision == viewStateRevision_;
        }
    }

    public bool CanUsePublishedFrameForInteraction(
        ulong sequence,
        ulong targetRevision,
        ulong viewStateRevision)
    {
        lock (gate_)
        {
            return CanUsePublishedFrameForInteractionLocked(sequence, targetRevision) &&
                viewStateRevision == viewStateRevision_;
        }
    }

    public bool MarkPublishedFramePresented(ulong sequence, ulong targetRevision) =>
        CanPresentPublishedFrame(sequence, targetRevision);

    public bool MarkPublishedFramePresented(
        ulong sequence,
        ulong targetRevision,
        ulong viewStateRevision) =>
        CanPresentPublishedFrame(sequence, targetRevision, viewStateRevision);

    public void RetryPublishedFrame(ViewportRenderRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        var requestRefresh = false;
        lock (gate_)
        {
            if (!isClosed_ && request.TargetRevision == targetRevision_)
            {
                requestRefresh = InvalidateLocked(
                    request.Reasons,
                    advancePresentationFence:
                        (request.Reasons & PresentationInvalidationReasons) !=
                        ViewportInvalidationReason.None);
            }
        }
        RaiseRefreshRequested(requestRefresh);
    }

    public bool TryBeginRender(ViewportExtent extent, out ViewportRenderRequest request) =>
        TryBeginRender(new ViewportRenderSize(extent, extent), out request);

    public bool CompleteRender(ulong sequence, ulong targetRevision, bool succeeded)
    {
        var requestRefresh = false;
        bool result;
        lock (gate_)
        {
            if (isClosed_ || sequence == 0 || sequence != inFlightSequence_ ||
                targetRevision != inFlightTargetRevision_)
            {
                return false;
            }

            var completionIsCurrent =
                CanPresentPublishedFrameLocked(sequence, targetRevision);
            if (!succeeded && completionIsCurrent)
            {
                requestRefresh = InvalidateLocked(
                    inFlightReasons_,
                    advancePresentationFence: false);
            }

            inFlightSequence_ = 0;
            inFlightTargetRevision_ = 0;
            inFlightReasons_ = ViewportInvalidationReason.None;
            result = succeeded && completionIsCurrent;
        }
        RaiseRefreshRequested(requestRefresh);
        return result;
    }

    public void Close()
    {
        lock (gate_)
        {
            isClosed_ = true;
            pendingReasons_ = ViewportInvalidationReason.None;
            inFlightSequence_ = 0;
            inFlightTargetRevision_ = 0;
            inFlightReasons_ = ViewportInvalidationReason.None;
            translateGizmo_ = null;
        }
    }

    private static readonly ViewportInvalidationReason AllInvalidationReasons =
        ViewportInvalidationReason.InitialFrame |
        ViewportInvalidationReason.TargetChanged |
        ViewportInvalidationReason.CameraChanged |
        ViewportInvalidationReason.ExtentChanged |
        ViewportInvalidationReason.Exposed |
        ViewportInvalidationReason.Realtime |
        ViewportInvalidationReason.SelectionChanged |
        ViewportInvalidationReason.GizmoChanged;

    private static readonly ViewportInvalidationReason PresentationInvalidationReasons =
        ViewportInvalidationReason.TargetChanged |
        ViewportInvalidationReason.Exposed |
        ViewportInvalidationReason.SelectionChanged;

    private static (ViewportDebugProxySnapshot[] Proxies, int TotalCount)
        CaptureDebugProxies(SceneDocumentSnapshot document)
    {
        var proxies = document.Entities
            .Take(ViewportRenderRequest.MaximumDebugProxyCount)
            .Select(entity => new ViewportDebugProxySnapshot(entity.ObjectId, entity.Transform))
            .ToArray();
        return (proxies, document.Entities.Count);
    }

    private static ViewportModelPickProxySnapshot[] CaptureModelPickProxies(
        SceneDocumentSnapshot document) =>
        document.Entities
            .Where(entity =>
                entity.Mesh == SceneMeshReference.DirectionalWedgeValidation)
            .Select(entity => new ViewportModelPickProxySnapshot(
                entity.ObjectId,
                DirectionalWedgeValidationLocalBounds,
                entity.Transform))
            .ToArray();

    private static ViewportAuthoredMeshSnapshot[] CaptureAuthoredMeshes(
        SceneDocumentSnapshot document)
    {
        var meshes = document.Entities
        .Where(entity => entity.Mesh is not null)
        .Select(entity => new ViewportAuthoredMeshSnapshot(
            entity.ObjectId,
            entity.RuntimeEntityId,
            entity.Mesh!.Value.AssetId,
            entity.Transform))
        .ToArray();
        if (meshes.Length > ViewportRenderRequest.MaximumAuthoredMeshCount)
        {
            throw new ArgumentOutOfRangeException(
                nameof(document),
                $"Viewport authored mesh count exceeds {ViewportRenderRequest.MaximumAuthoredMeshCount}.");
        }
        return meshes;
    }

    private static Dictionary<Guid, TransformValue> CaptureEntityTransforms(
        SceneDocumentSnapshot document) =>
        document.Entities.ToDictionary(entity => entity.ObjectId, entity => entity.Transform);

    private ViewportTranslateGizmoState? CreateTranslateGizmoLocked(Guid? objectId) =>
        kind_ == ViewportRenderKind.Scene && objectId is { } selected &&
        entityTransforms_.TryGetValue(selected, out var transform)
            ? new ViewportTranslateGizmoState(selected, transform)
            : null;

    private ViewportSessionSnapshot SnapshotLocked() => new(
        sessionId_,
        kind_,
        targetKind_,
        targetId_,
        targetRevision_,
        viewStateRevision_,
        selectedObjectId_,
        lastSequence_,
        minimumPresentableSequence_,
        inFlightSequence_ != 0,
        pendingReasons_,
        isClosed_);

    private bool InvalidateLocked(
        ViewportInvalidationReason reasons,
        bool advancePresentationFence)
    {
        if (advancePresentationFence)
        {
            minimumPresentableSequence_ = checked(lastSequence_ + 1U);
        }
        var wasClean = pendingReasons_ == ViewportInvalidationReason.None;
        pendingReasons_ |= reasons;
        return wasClean;
    }

    private bool CanPresentPublishedFrameLocked(ulong sequence, ulong targetRevision) =>
        !isClosed_ && sequence >= minimumPresentableSequence_ &&
        sequence <= lastSequence_ && targetRevision == targetRevision_;

    private bool CanUsePublishedFrameForInteractionLocked(
        ulong sequence,
        ulong targetRevision) =>
        minimumInteractiveCameraSequence_ != 0 &&
        sequence >= minimumInteractiveCameraSequence_ &&
        CanPresentPublishedFrameLocked(sequence, targetRevision);

    private void MarkCameraPublishedLocked(ulong sequence)
    {
        if (minimumInteractiveCameraSequence_ == 0)
        {
            minimumInteractiveCameraSequence_ = sequence;
        }
    }

    private void RaiseRefreshRequested(bool requestRefresh)
    {
        if (requestRefresh)
        {
            RefreshRequested?.Invoke(this, EventArgs.Empty);
        }
    }

    private void ThrowIfClosed()
    {
        ObjectDisposedException.ThrowIf(isClosed_, this);
    }
}
