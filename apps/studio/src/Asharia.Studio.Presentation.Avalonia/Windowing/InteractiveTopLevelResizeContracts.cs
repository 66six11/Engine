using System;
using Avalonia;
using Avalonia.Controls;

namespace Asharia.Studio.Presentation.Avalonia.Windowing;

/// <summary>
/// Exposes the platform resize adapter selected by the application composition root.
/// </summary>
public interface IInteractiveTopLevelResizeAdapterProvider
{
    IInteractiveTopLevelResizeAdapterFactory? InteractiveTopLevelResizeAdapterFactory { get; }
}

/// <summary>
/// Creates an optional native interactive-resize attachment for a top level.
/// </summary>
public interface IInteractiveTopLevelResizeAdapterFactory
{
    IInteractiveTopLevelResizeAttachment? TryAttach(
        TopLevel topLevel,
        IInteractiveTopLevelResizeSink sink);
}

/// <summary>
/// Owns a platform top-level hook for the lifetime of one attached presentation host.
/// </summary>
public interface IInteractiveTopLevelResizeAttachment : IDisposable
{
}

/// <summary>
/// Receives platform-neutral, latest-wins top-level resize proposals.
/// </summary>
public interface IInteractiveTopLevelResizeSink
{
    Size CurrentWorkspaceSize { get; }

    bool CanStartPrecommittedResize();

    bool TryQueuePrecommittedResize(
        Size targetSize,
        IInteractiveTopLevelResizeCommit outerCommit);
}

/// <summary>
/// Participates in the logical transaction boundary that advances exact viewport fronts.
/// </summary>
public interface IInteractiveTopLevelResizeCommit
{
    void Apply();

    void Rollback();

    void Accept();

    bool IsCurrent();
}

/// <summary>
/// Projects physical outer-window proposals into the logical workspace contract.
/// </summary>
public static class InteractiveTopLevelResizeProjection
{
    public static bool TryProjectWorkspaceTarget(
        Size currentOuterPixels,
        Size currentClientPixels,
        Size proposedOuterPixels,
        Size fixedLogicalInsets,
        double scaling,
        out Size targetSize)
    {
        targetSize = default;
        if (!double.IsFinite(scaling) || scaling <= 0)
        {
            return false;
        }

        var targetClientWidth = proposedOuterPixels.Width -
            (currentOuterPixels.Width - currentClientPixels.Width);
        var targetClientHeight = proposedOuterPixels.Height -
            (currentOuterPixels.Height - currentClientPixels.Height);
        if (targetClientWidth <= 0 || targetClientHeight <= 0)
        {
            return false;
        }

        var targetWidth = (targetClientWidth / scaling) - fixedLogicalInsets.Width;
        var targetHeight = (targetClientHeight / scaling) - fixedLogicalInsets.Height;
        if (!double.IsFinite(targetWidth) || !double.IsFinite(targetHeight) ||
            targetWidth <= 0 || targetHeight <= 0)
        {
            return false;
        }

        targetSize = new Size(targetWidth, targetHeight);
        return true;
    }
}
