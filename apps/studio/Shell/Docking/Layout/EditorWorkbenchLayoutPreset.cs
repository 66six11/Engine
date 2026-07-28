using System.Collections.Generic;
using Asharia.Editor.Panels;
using Avalonia.Controls;
using Avalonia.Layout;

namespace Editor.Shell.Docking.Layout;

internal static class EditorWorkbenchLayoutPreset
{
    private const string NodeKindSplit = "Split";
    private const string NodeKindWindow = "Window";

    public static EditorDockLayoutSnapshot CreateDefault()
    {
        var hierarchy = CreateWindow(
            "node-left",
            "owned-dock-left",
            "Hierarchy",
            EditorDockArea.Left,
            "Scene tree",
            ["hierarchy"]);
        var project = CreateWindow(
            "node-project",
            "owned-dock-project",
            "Project",
            EditorDockArea.Left,
            "Project content",
            ["project"]);
        var leftColumn = CreateSplit(
            "split-left-tools",
            Orientation.Vertical,
            hierarchy,
            project,
            Star(1),
            Star(1));

        return CreateWorkbenchSnapshot(leftColumn);
    }

    public static EditorDockLayoutSnapshot CreateCompact()
    {
        var leftTabs = CreateWindow(
            "node-left",
            "owned-dock-left",
            "Project",
            EditorDockArea.Left,
            "Hierarchy and project content",
            ["hierarchy", "project"]);

        return CreateWorkbenchSnapshot(leftTabs);
    }

    private static EditorDockLayoutSnapshot CreateWorkbenchSnapshot(
        EditorDockLayoutNodeSnapshot left)
    {
        var center = CreateWindow(
            "node-center",
            "owned-dock-center",
            "Scene View",
            EditorDockArea.Center,
            "Primary work area",
            ["scene-view"]);
        var right = CreateWindow(
            "node-right",
            "owned-dock-right",
            "Inspector",
            EditorDockArea.Right,
            "Selection context",
            ["inspector"]);
        var centerAndInspector = CreateSplit(
            "split-work-inspector",
            Orientation.Horizontal,
            center,
            right,
            Star(1),
            Pixels(320));

        return new EditorDockLayoutSnapshot
        {
            Version = 1,
            ActiveWindowId = "owned-dock-center",
            Root = CreateSplit(
                "split-left-work",
                Orientation.Horizontal,
                left,
                centerAndInspector,
                Pixels(260),
                Star(1)),
        };
    }

    private static EditorDockLayoutNodeSnapshot CreateWindow(
        string nodeId,
        string windowId,
        string title,
        EditorDockArea area,
        string role,
        List<string> tabIds)
    {
        return new EditorDockLayoutNodeSnapshot
        {
            Kind = NodeKindWindow,
            Id = nodeId,
            WindowId = windowId,
            WindowTitle = title,
            WindowArea = area,
            WindowRole = role,
            TabIds = tabIds,
            ActiveTabId = tabIds[0],
        };
    }

    private static EditorDockLayoutNodeSnapshot CreateSplit(
        string id,
        Orientation orientation,
        EditorDockLayoutNodeSnapshot first,
        EditorDockLayoutNodeSnapshot second,
        EditorDockGridLengthSnapshot firstLength,
        EditorDockGridLengthSnapshot secondLength)
    {
        return new EditorDockLayoutNodeSnapshot
        {
            Kind = NodeKindSplit,
            Id = id,
            Orientation = orientation,
            First = first,
            Second = second,
            FirstLength = firstLength,
            SecondLength = secondLength,
        };
    }

    private static EditorDockGridLengthSnapshot Pixels(double value)
    {
        return new EditorDockGridLengthSnapshot
        {
            Value = value,
            Unit = GridUnitType.Pixel,
        };
    }

    private static EditorDockGridLengthSnapshot Star(double value)
    {
        return new EditorDockGridLengthSnapshot
        {
            Value = value,
            Unit = GridUnitType.Star,
        };
    }
}
