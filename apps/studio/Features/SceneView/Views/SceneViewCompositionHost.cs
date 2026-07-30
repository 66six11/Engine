using System;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Vector3 = System.Numerics.Vector3;

namespace Editor.Features.SceneView.Views;

internal sealed class SceneViewCompositionHost : Control
{
    private readonly SemaphoreSlim surfaceUpdateGate_ =
        new(initialCount: 1, maxCount: 1);
    private readonly SceneViewCompositionCommitState commitState_ = new();
    private CompositionSurfaceVisual? visual_;
    private CompositionDrawingSurface? surface_;
    private Task resourceReleaseTask_ = Task.CompletedTask;
    private bool isPlacementUpdateQueued_;

    public CompositionDrawingSurface? Surface => surface_;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        AttachCompositionVisual();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        ElementComposition.SetElementChildVisual(this, null);
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == BoundsProperty)
        {
            QueueCommittedFramePlacement();
        }
    }

    public async Task<bool> TryCommitFrameAsync(
        Size frameSizeDip,
        Func<bool> isCurrent,
        Func<CompositionDrawingSurface, Task> updateSurface)
    {
        ArgumentNullException.ThrowIfNull(isCurrent);
        ArgumentNullException.ThrowIfNull(updateSurface);
        EnsureUiThread();

        await surfaceUpdateGate_.WaitAsync();
        try
        {
            if (!isCurrent() || !Bounds.Size.Equals(frameSizeDip))
            {
                return false;
            }

            return await TryCommitFrameCoreAsync(
                frameSizeDip,
                isCurrent,
                updateSurface);
        }
        finally
        {
            surfaceUpdateGate_.Release();
        }
    }

    private Task<bool> TryCommitFrameCoreAsync(
        Size frameSizeDip,
        Func<bool> isCurrent,
        Func<CompositionDrawingSurface, Task> updateSurface)
    {
        if (visual_ is null || surface_ is null)
        {
            return Task.FromException<bool>(
                new InvalidOperationException("Scene View composition surface is unavailable."));
        }

        var visual = visual_;
        var surface = surface_;
        var completion = new TaskCompletionSource<bool>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        visual.Compositor.RequestCompositionUpdate(
            () =>
            {
                if (!isCurrent() || !Bounds.Size.Equals(frameSizeDip))
                {
                    completion.TrySetResult(false);
                    return;
                }

                Task updateTask;
                var commitVersion = commitState_.BeginAttempt();
                try
                {
                    updateTask = updateSurface(surface);
                    ApplyFramePlacement(visual, frameSizeDip);
                    _ = CompleteUpdateAsync(
                        updateTask,
                        visual,
                        frameSizeDip,
                        commitVersion,
                        completion);
                }
                catch (Exception ex)
                {
                    if (commitState_.TryGetRollbackTarget(
                            commitVersion,
                            out var rollbackFrameSizeDip))
                    {
                        ApplyFramePlacement(visual, rollbackFrameSizeDip);
                    }

                    completion.TrySetException(ex);
                }
            });
        return completion.Task;
    }

    public Task ReleaseCompositionResourcesAsync(Task presentationDrain)
    {
        ArgumentNullException.ThrowIfNull(presentationDrain);
        EnsureUiThread();

        ElementComposition.SetElementChildVisual(this, null);
        var surface = surface_;
        visual_ = null;
        surface_ = null;
        commitState_.Reset();
        isPlacementUpdateQueued_ = false;

        var prerequisite = Task.WhenAll(resourceReleaseTask_, presentationDrain);
        resourceReleaseTask_ =
            surface is null
                ? prerequisite
                : DisposeSurfaceAfterAsync(surface, prerequisite);
        return resourceReleaseTask_;
    }

    private void AttachCompositionVisual()
    {
        if (visual_ is not null)
        {
            return;
        }

        var selfVisual = ElementComposition.GetElementVisual(this);
        if (selfVisual is null)
        {
            return;
        }

        var compositor = selfVisual.Compositor;
        surface_ = compositor.CreateDrawingSurface();
        visual_ = compositor.CreateSurfaceVisual();
        visual_.Size = default;
        visual_.Offset = Vector3.Zero;
        visual_.Surface = surface_;
        ElementComposition.SetElementChildVisual(this, visual_);
    }

    private void QueueCommittedFramePlacement()
    {
        if (visual_ is not { } visual ||
            commitState_.LastSuccessfulFrameSizeDip is not { } lastSuccessfulFrameSizeDip ||
            isPlacementUpdateQueued_)
        {
            return;
        }

        isPlacementUpdateQueued_ = true;
        visual.Compositor.RequestCompositionUpdate(
            () =>
            {
                isPlacementUpdateQueued_ = false;
                if (!ReferenceEquals(visual_, visual) ||
                    commitState_.LastSuccessfulFrameSizeDip !=
                        lastSuccessfulFrameSizeDip)
                {
                    QueueCommittedFramePlacement();
                    return;
                }

                ApplyFramePlacement(visual, lastSuccessfulFrameSizeDip);
            });
    }

    private static Vector ToVector(Size size)
    {
        return new Vector(size.Width, size.Height);
    }

    private async Task CompleteUpdateAsync(
        Task updateTask,
        CompositionSurfaceVisual visual,
        Size frameSizeDip,
        ulong commitVersion,
        TaskCompletionSource<bool> completion)
    {
        Exception? updateFailure = null;
        try
        {
            await updateTask;
        }
        catch (Exception ex)
        {
            updateFailure = ex;
        }

        try
        {
            visual.Compositor.RequestCompositionUpdate(
                () =>
                {
                    if (ReferenceEquals(visual_, visual))
                    {
                        if (updateFailure is null)
                        {
                            if (commitState_.CompleteSuccessfulAttempt(
                                    commitVersion,
                                    frameSizeDip))
                            {
                                ApplyFramePlacement(visual, frameSizeDip);
                            }
                        }
                        else if (commitState_.TryGetRollbackTarget(
                                     commitVersion,
                                     out var rollbackFrameSizeDip))
                        {
                            ApplyFramePlacement(
                                visual,
                                rollbackFrameSizeDip);
                        }
                    }

                    if (updateFailure is null)
                    {
                        completion.TrySetResult(true);
                    }
                    else
                    {
                        completion.TrySetException(updateFailure);
                    }
                });
        }
        catch (Exception ex)
        {
            completion.TrySetException(updateFailure ?? ex);
        }
    }

    private void ApplyFramePlacement(
        CompositionSurfaceVisual visual,
        Size? frameSizeDip)
    {
        if (frameSizeDip is not { } size)
        {
            visual.Size = default;
            visual.Offset = Vector3.Zero;
            return;
        }

        visual.Size = ToVector(size);
        visual.Offset =
            new Vector3(
                (float)((Bounds.Width - size.Width) / 2d),
                (float)((Bounds.Height - size.Height) / 2d),
                0f);
    }

    private static async Task DisposeSurfaceAfterAsync(
        CompositionDrawingSurface surface,
        Task prerequisite)
    {
        try
        {
            await prerequisite;
        }
        finally
        {
            surface.Dispose();
        }
    }

    private static void EnsureUiThread()
    {
        if (!Dispatcher.UIThread.CheckAccess())
        {
            throw new InvalidOperationException(
                "Scene View composition resources must be accessed on the UI dispatcher.");
        }
    }
}
