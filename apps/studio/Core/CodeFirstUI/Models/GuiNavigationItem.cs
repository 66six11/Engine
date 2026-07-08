using System;
using System.Linq;

namespace Editor.Core.CodeFirstUI.Models;

public sealed record GuiNavigationItem
{
    public GuiNavigationItem(string route, string label)
    {
        if (string.IsNullOrWhiteSpace(route))
        {
            throw new ArgumentException("Navigation route must not be empty.", nameof(route));
        }

        if (route
            .Split('/')
            .Any(segment => string.IsNullOrWhiteSpace(segment)))
        {
            throw new ArgumentException("Navigation route must not contain empty segments.", nameof(route));
        }

        if (string.IsNullOrWhiteSpace(label))
        {
            throw new ArgumentException("Navigation label must not be empty.", nameof(label));
        }

        Route = route;
        Label = label;
    }

    public string Route { get; }

    public string Label { get; }
}
