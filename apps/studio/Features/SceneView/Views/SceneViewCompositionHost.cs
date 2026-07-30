using System;
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
    private readonly SceneViewSurfaceUpdateGate surfaceUpdateGate_ = new();
    private CompositionSurfaceVisual? visual_;
    private CompositionDrawingSurface? surface_;
    private Task resourceReleaseTask_ = Task.CompletedTask;

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
        if (change.Property == BoundsProperty && visual_ is { } visual)
        {
            ApplyFramePlacement(visual);
        }
    }

    public Task<bool> TryCommitFrameAsync(
        Func<bool> canPresent,
        Func<bool> tryAcceptPresentation,
        Func<CompositionDrawingSurface, Task> updateSurface)
    {
        ArgumentNullException.ThrowIfNull(canPresent);
        ArgumentNullException.ThrowIfNull(tryAcceptPresentation);
        ArgumentNullException.ThrowIfNull(updateSurface);
        EnsureUiThread();

        return surfaceUpdateGate_.RunAsync(
            canPresent,
            () => TryCommitFrameCoreAsync(canPresent, updateSurface),
            tryAcceptPresentation);
    }

    private Task<bool> TryCommitFrameCoreAsync(
        Func<bool> canPresent,
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
                if (!canPresent())
                {
                    completion.TrySetResult(false);
                    return;
                }

                Task updateTask;
                try
                {
                    updateTask = updateSurface(surface);
                    ApplyFramePlacement(visual);
                    _ = CompleteUpdateAsync(
                        updateTask,
                        visual,
                        completion);
                }
                catch (Exception ex)
                {
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
        ApplyFramePlacement(visual_);
        visual_.Surface = surface_;
        ElementComposition.SetElementChildVisual(this, visual_);
    }

    private static Vector ToVector(Size size)
    {
        return new Vector(size.Width, size.Height);
    }

    private async Task CompleteUpdateAsync(
        Task updateTask,
        CompositionSurfaceVisual visual,
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

        EnsureUiThread();
        if (updateFailure is not null)
        {
            completion.TrySetException(updateFailure);
            return;
        }

        var updated = ReferenceEquals(visual_, visual);
        if (updated)
        {
            ApplyFramePlacement(visual);
        }

        completion.TrySetResult(updated);
    }

    private void ApplyFramePlacement(CompositionSurfaceVisual visual)
    {
        visual.Size = ToVector(Bounds.Size);
        visual.Offset = Vector3.Zero;
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
