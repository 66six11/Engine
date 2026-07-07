using System;
using System.Threading.Tasks;
using Editor.Core.Interop.Viewports.Adapters;

namespace Editor.Features.SceneView.Interop;

internal sealed class SceneViewNativeViewportLifecycle
{
    private Task? pendingPresent_;

    public bool CanBeginPresent =>
        (pendingPresent_ is null || pendingPresent_.IsCompleted) &&
        ViewportNativePresentDrain.CanBeginPresent;

    public bool TryBeginPresent(Func<Task> presentTaskFactory)
    {
        ArgumentNullException.ThrowIfNull(presentTaskFactory);
        if (!CanBeginPresent)
        {
            return false;
        }

        var presentTask = presentTaskFactory();
        ArgumentNullException.ThrowIfNull(presentTask);
        pendingPresent_ = presentTask;
        _ = ViewportNativePresentDrain.TrackAsync(presentTask);
        return true;
    }
}
