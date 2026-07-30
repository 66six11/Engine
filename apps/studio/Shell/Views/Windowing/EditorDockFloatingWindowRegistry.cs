using System;
using System.Collections.Generic;
using Avalonia;
using Editor.Shell.Docking.Layout;
using Editor.Shell.Lifecycle;
using Editor.Shell.ViewModels.Docking;
using Editor.Shell.Views.Docking;

namespace Editor.Shell.Views.Windowing;

internal static class EditorDockFloatingWindowRegistry
{
    private static readonly List<WeakReference<EditorDockFloatingWindow>> Windows = [];

    public static event EventHandler? DockContentChanged;

    public static void Register(EditorDockFloatingWindow window)
    {
        Prune();
        foreach (var reference in Windows)
        {
            if (reference.TryGetTarget(out var existing)
                && ReferenceEquals(existing, window))
            {
                return;
            }
        }

        Windows.Add(new WeakReference<EditorDockFloatingWindow>(window));
        SubscribeDockContentChanged(window);
        RaiseDockContentChanged();
    }

    public static void Unregister(EditorDockFloatingWindow window)
    {
        for (var index = Windows.Count - 1; index >= 0; index--)
        {
            if (!Windows[index].TryGetTarget(out var existing)
                || ReferenceEquals(existing, window))
            {
                if (existing is not null)
                {
                    UnsubscribeDockContentChanged(existing);
                }

                Windows.RemoveAt(index);
            }
        }

        RaiseDockContentChanged();
    }

    public static IReadOnlyList<EditorDockFloatingWindowSnapshot> CaptureSnapshots()
    {
        Prune();
        var snapshots = new List<EditorDockFloatingWindowSnapshot>();
        foreach (var reference in Windows)
        {
            if (!TryGetOpenWindow(reference, out var window)
                || window.DataContext is not EditorDockFloatingWindowViewModel viewModel
                || !viewModel.DockWorkspace.HasDockContent())
            {
                continue;
            }

            var workspaceSnapshot = viewModel.DockWorkspace.CaptureLayoutSnapshot();
            if (workspaceSnapshot.Root is null)
            {
                continue;
            }

            var bounds = GetWindowBounds(window);
            snapshots.Add(new EditorDockFloatingWindowSnapshot
            {
                X = bounds.X,
                Y = bounds.Y,
                Width = bounds.Width,
                Height = bounds.Height,
                ActiveWindowId = workspaceSnapshot.ActiveWindowId,
                Root = workspaceSnapshot.Root,
            });
        }

        return snapshots;
    }

    public static void CloseAll()
    {
        var exceptions = new CallbackExceptionBatch();
        exceptions.Capture(Prune);
        var closeActions = new List<Action>();
        foreach (var reference in Windows.ToArray())
        {
            if (reference.TryGetTarget(out var window))
            {
                closeActions.Add(window.Close);
            }
        }

        CloseAllCore(
            closeActions,
            () =>
            {
                Windows.Clear();
                RaiseDockContentChanged();
            },
            exceptions);
        exceptions.ThrowIfAny();
    }

    internal static void CloseAllCore(
        IEnumerable<Action> closeActions,
        Action onCompleted,
        CallbackExceptionBatch? exceptions = null)
    {
        var callbackExceptions = exceptions ?? new CallbackExceptionBatch();
        foreach (var closeAction in closeActions)
        {
            callbackExceptions.Capture(closeAction);
        }

        callbackExceptions.Capture(onCompleted);
        if (exceptions is null)
        {
            callbackExceptions.ThrowIfAny();
        }
    }

    public static bool TryActivatePanel(string panelId)
    {
        Prune();
        foreach (var reference in Windows)
        {
            if (!TryGetOpenWindow(reference, out var window)
                || window.DataContext is not EditorDockFloatingWindowViewModel viewModel
                || !viewModel.DockWorkspace.ActivatePanel(panelId))
            {
                continue;
            }

            window.Activate();
            return true;
        }

        return false;
    }

    public static bool ContainsPanel(string panelId)
    {
        if (string.IsNullOrWhiteSpace(panelId))
        {
            return false;
        }

        Prune();
        foreach (var reference in Windows)
        {
            if (TryGetOpenWindow(reference, out var window)
                && window.DataContext is EditorDockFloatingWindowViewModel viewModel
                && viewModel.DockWorkspace.ContainsPanel(panelId))
            {
                return true;
            }
        }

        return false;
    }

    public static bool TryClosePanel(string panelId)
    {
        if (string.IsNullOrWhiteSpace(panelId))
        {
            return false;
        }

        Prune();
        foreach (var reference in Windows)
        {
            if (!TryGetOpenWindow(reference, out var window)
                || window.DataContext is not EditorDockFloatingWindowViewModel viewModel
                || !viewModel.DockWorkspace.ContainsPanel(panelId))
            {
                continue;
            }

            var exceptions = new CallbackExceptionBatch();
            var closed = false;
            exceptions.Capture(
                () => closed = viewModel.DockWorkspace.ClosePanel(panelId));
            if (!viewModel.DockWorkspace.HasDockContent())
            {
                exceptions.Capture(window.Close);
            }

            exceptions.ThrowIfAny();
            return closed;
        }

        return false;
    }

    private static bool TryGetOpenWindow(
        WeakReference<EditorDockFloatingWindow> reference,
        out EditorDockFloatingWindow window)
    {
        if (reference.TryGetTarget(out var target)
            && target.IsVisible)
        {
            window = target;
            return true;
        }

        window = null!;
        return false;
    }

    private static Rect GetWindowBounds(EditorDockFloatingWindow window)
    {
        return new Rect(
            window.Position.X,
            window.Position.Y,
            Math.Max(240, window.Width),
            Math.Max(180, window.Height));
    }

    private static void Prune()
    {
        for (var index = Windows.Count - 1; index >= 0; index--)
        {
            if (!Windows[index].TryGetTarget(out var window))
            {
                if (window is not null)
                {
                    UnsubscribeDockContentChanged(window);
                }

                Windows.RemoveAt(index);
            }
        }
    }

    private static void SubscribeDockContentChanged(EditorDockFloatingWindow window)
    {
        if (window.DataContext is EditorDockFloatingWindowViewModel viewModel)
        {
            viewModel.DockWorkspace.DockContentChanged += OnFloatingDockContentChanged;
        }
    }

    private static void UnsubscribeDockContentChanged(EditorDockFloatingWindow window)
    {
        if (window.DataContext is EditorDockFloatingWindowViewModel viewModel)
        {
            viewModel.DockWorkspace.DockContentChanged -= OnFloatingDockContentChanged;
        }
    }

    private static void OnFloatingDockContentChanged(object? sender, EventArgs e)
    {
        RaiseDockContentChanged();
    }

    private static void RaiseDockContentChanged()
    {
        DockContentChanged?.Invoke(null, EventArgs.Empty);
    }
}
