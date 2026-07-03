using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Layout;
using Editor.Core.Models;
using Editor.Core.Models.Panels;

namespace Editor.Shell.Docking.Layout;

public sealed class EditorDockLayoutSnapshot
{
    public int Version { get; set; } = 1;

    public string? ActiveWindowId { get; set; }

    public EditorDockLayoutNodeSnapshot? Root { get; set; }

    public List<EditorDockFloatingWindowSnapshot> FloatingWindows { get; set; } = [];
}

public sealed class EditorDockFloatingWindowSnapshot
{
    public double X { get; set; }

    public double Y { get; set; }

    public double Width { get; set; }

    public double Height { get; set; }

    public string? ActiveWindowId { get; set; }

    public EditorDockLayoutNodeSnapshot? Root { get; set; }
}

public sealed class EditorDockLayoutNodeSnapshot
{
    public string Kind { get; set; } = string.Empty;

    public string Id { get; set; } = string.Empty;

    public string? WindowId { get; set; }

    public string? WindowTitle { get; set; }

    public DockArea WindowArea { get; set; }

    public string? WindowRole { get; set; }

    public List<string> TabIds { get; set; } = [];

    public string? ActiveTabId { get; set; }

    public Orientation Orientation { get; set; }

    public EditorDockGridLengthSnapshot? FirstLength { get; set; }

    public EditorDockGridLengthSnapshot? SecondLength { get; set; }

    public EditorDockLayoutNodeSnapshot? First { get; set; }

    public EditorDockLayoutNodeSnapshot? Second { get; set; }
}

public sealed class EditorDockGridLengthSnapshot
{
    public double Value { get; set; }

    public GridUnitType Unit { get; set; }
}
