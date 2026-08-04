using System;
using System.Linq;
using Asharia.Studio.Application.Scenes;

namespace Asharia.Studio.Application.Viewports;

public sealed class ViewportSession
{
    private readonly object gate_ = new();
    private readonly ViewportSessionId sessionId_;
    private readonly ViewportRenderKind kind_;
    private readonly ViewportTargetKind targetKind_ = ViewportTargetKind.DocumentScene;
    private Guid targetId_;
    private ulong targetRevision_;
    private ViewportCameraSnapshot camera_;
    private ViewportDebugProxySnapshot[] debugProxies_;
    private int totalDebugProxyCount_;
    private ViewportExtent? lastExtent_;
    private ViewportInvalidationReason pendingReasons_ = ViewportInvalidationReason.InitialFrame;
    private ulong lastSequence_;
    private ulong inFlightSequence_;
    private ulong inFlightTargetRevision_;
    private ViewportInvalidationReason inFlightReasons_;
    private bool isClosed_;

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

    public void SynchronizeDocument(SceneDocumentSnapshot document)
    {
        ArgumentNullException.ThrowIfNull(document);
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

            targetRevision_ = document.Revision;
            (debugProxies_, totalDebugProxyCount_) = CaptureDebugProxies(document);
            pendingReasons_ |= ViewportInvalidationReason.TargetChanged;
        }
    }

    public void SetCamera(ViewportCameraSnapshot camera)
    {
        ArgumentNullException.ThrowIfNull(camera);
        lock (gate_)
        {
            ThrowIfClosed();
            if (camera_ == camera)
            {
                return;
            }

            camera_ = camera;
            pendingReasons_ |= ViewportInvalidationReason.CameraChanged;
        }
    }

    public void Invalidate(ViewportInvalidationReason reasons)
    {
        if (reasons == ViewportInvalidationReason.None ||
            (reasons & ~AllInvalidationReasons) != ViewportInvalidationReason.None)
        {
            throw new ArgumentOutOfRangeException(nameof(reasons), reasons, null);
        }

        lock (gate_)
        {
            ThrowIfClosed();
            pendingReasons_ |= reasons;
        }
    }

    public bool TryBeginRender(ViewportExtent extent, out ViewportRenderRequest request)
    {
        if (!extent.IsRenderable)
        {
            throw new ArgumentOutOfRangeException(nameof(extent));
        }

        lock (gate_)
        {
            request = null!;
            if (isClosed_ || inFlightSequence_ != 0)
            {
                return false;
            }
            if (lastExtent_ != extent)
            {
                lastExtent_ = extent;
                pendingReasons_ |= ViewportInvalidationReason.ExtentChanged;
            }
            if (pendingReasons_ == ViewportInvalidationReason.None)
            {
                return false;
            }

            var sequence = checked(++lastSequence_);
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
                extent,
                camera_,
                reasons,
                debugProxies_,
                totalDebugProxyCount_);
            return true;
        }
    }

    public bool CompleteRender(ulong sequence, ulong targetRevision, bool succeeded)
    {
        lock (gate_)
        {
            if (isClosed_ || sequence == 0 || sequence != inFlightSequence_ ||
                targetRevision != inFlightTargetRevision_)
            {
                return false;
            }

            var completionIsCurrent = targetRevision == targetRevision_;
            if (!succeeded && completionIsCurrent)
            {
                pendingReasons_ |= inFlightReasons_;
            }

            inFlightSequence_ = 0;
            inFlightTargetRevision_ = 0;
            inFlightReasons_ = ViewportInvalidationReason.None;
            return succeeded && completionIsCurrent;
        }
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
        }
    }

    private static readonly ViewportInvalidationReason AllInvalidationReasons =
        ViewportInvalidationReason.InitialFrame |
        ViewportInvalidationReason.TargetChanged |
        ViewportInvalidationReason.CameraChanged |
        ViewportInvalidationReason.ExtentChanged |
        ViewportInvalidationReason.Exposed;

    private static (ViewportDebugProxySnapshot[] Proxies, int TotalCount)
        CaptureDebugProxies(SceneDocumentSnapshot document)
    {
        var proxies = document.Entities
            .Take(ViewportRenderRequest.MaximumDebugProxyCount)
            .Select(entity => new ViewportDebugProxySnapshot(entity.ObjectId, entity.Transform))
            .ToArray();
        return (proxies, document.Entities.Count);
    }

    private ViewportSessionSnapshot SnapshotLocked() => new(
        sessionId_,
        kind_,
        targetKind_,
        targetId_,
        targetRevision_,
        lastSequence_,
        inFlightSequence_ != 0,
        pendingReasons_,
        isClosed_);

    private void ThrowIfClosed()
    {
        ObjectDisposedException.ThrowIf(isClosed_, this);
    }
}
