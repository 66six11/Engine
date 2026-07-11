using System;
using Asharia.Editor.UI.CodeFirst.Models;

namespace Editor.Core.CodeFirstUI.Authoring;

public sealed record GuiNavigationPage
{
    public GuiNavigationPage(
        string route,
        string label,
        Action<EditorGui> draw)
    {
        ArgumentNullException.ThrowIfNull(draw);

        Item = new GuiNavigationItem(route, label);
        Draw = draw;
    }

    public string Route => Item.Route;

    public string Label => Item.Label;

    public Action<EditorGui> Draw { get; }

    internal GuiNavigationItem Item { get; }
}
