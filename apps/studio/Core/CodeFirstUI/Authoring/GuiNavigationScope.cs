using System;
using System.Collections.Generic;
using System.Linq;

namespace Editor.Core.CodeFirstUI;

public sealed class GuiNavigationScope : IDisposable
{
    private readonly IDisposable innerScope_;
    private readonly IReadOnlyDictionary<string, GuiNavigationPage> pagesByRoute_;
    private bool isDisposed_;

    public GuiNavigationScope(
        IDisposable innerScope,
        string? selectedRoute,
        IReadOnlyList<GuiNavigationPage> pages)
    {
        ArgumentNullException.ThrowIfNull(innerScope);
        ArgumentNullException.ThrowIfNull(pages);

        innerScope_ = innerScope;
        SelectedRoute = selectedRoute;
        pagesByRoute_ = pages.ToDictionary(page => page.Route, StringComparer.Ordinal);
    }

    public string? SelectedRoute { get; }

    public bool DrawSelected(EditorGui gui)
    {
        ArgumentNullException.ThrowIfNull(gui);

        if (SelectedRoute is null
            || !pagesByRoute_.TryGetValue(SelectedRoute, out var page))
        {
            return false;
        }

        page.Draw(gui);
        return true;
    }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        innerScope_.Dispose();
        isDisposed_ = true;
    }
}
