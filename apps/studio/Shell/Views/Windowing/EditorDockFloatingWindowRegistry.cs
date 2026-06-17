using System;
using System.Collections.Generic;
using Avalonia;
using Editor.Shell.Docking;
using Editor.Shell.ViewModels;

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
        Prune();
        foreach (var reference in Windows.ToArray())
        {
            if (reference.TryGetTarget(out var window))
            {
                window.Close();
            }
        }

        Windows.Clear();
        RaiseDockContentChanged();
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
                || !viewModel.DockWorkspace.ClosePanel(panelId))
            {
                continue;
            }

            if (!viewModel.DockWorkspace.HasDockContent())
            {
                window.Close();
            }

            return true;
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
