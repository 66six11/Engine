using System;
using System.Collections.Generic;

namespace Asharia.Editor.UI.CodeFirst.Authoring;

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
        pagesByRoute_ = CreatePagesByRoute(pages);
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

    private static IReadOnlyDictionary<string, GuiNavigationPage> CreatePagesByRoute(
        IReadOnlyList<GuiNavigationPage> pages)
    {
        var pagesByRoute = new Dictionary<string, GuiNavigationPage>(StringComparer.Ordinal);
        foreach (var page in pages)
        {
            pagesByRoute.TryAdd(page.Route, page);
        }

        return pagesByRoute;
    }
}
