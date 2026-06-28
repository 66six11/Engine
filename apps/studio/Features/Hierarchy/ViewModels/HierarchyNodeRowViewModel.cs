using System;
using CommunityToolkit.Mvvm.Input;
using Editor.Features.Hierarchy.Models;
using Editor.Shell.Icons;
using Editor.UI.Controls.Tree;

namespace Editor.Features.Hierarchy.ViewModels;

public sealed class HierarchyNodeRowViewModel
{
    private const string ExpandedIconKey = "studio.ui.chevron-down";
    private const string CollapsedIconKey = "studio.ui.chevron-right";

    public HierarchyNodeRowViewModel(
        HierarchyNodeModel node,
        int depth,
        bool hasChildren,
        bool isExpanded,
        bool isLastSibling,
        ulong ancestorContinuationMask,
        bool isSearchMatch,
        IRelayCommand<HierarchyNodeRowViewModel> toggleExpandedCommand)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(toggleExpandedCommand);

        Node = node;
        Depth = Math.Max(0, depth);
        HasChildren = hasChildren;
        IsExpanded = isExpanded;
        IsLastSibling = isLastSibling;
        AncestorContinuationMask = ancestorContinuationMask;
        IsSearchMatch = isSearchMatch;
        ToggleExpandedCommand = toggleExpandedCommand;
    }

    public HierarchyNodeModel Node { get; }

    public string Id => Node.Id;

    public string DisplayName => Node.DisplayName;

    public string Kind => Node.Kind;

    public string IconKey => string.IsNullOrWhiteSpace(Node.IconKey)
        ? EditorIconKey.ObjectDefault
        : Node.IconKey;

    public int Depth { get; }

    public bool HasChildren { get; }

    public bool IsExpanded { get; }

    public bool IsLastSibling { get; }

    public ulong AncestorContinuationMask { get; }

    public bool IsSearchMatch { get; }

    public IRelayCommand<HierarchyNodeRowViewModel> ToggleExpandedCommand { get; }

    public double IndentWidth => Depth * EditorTreeMetrics.IndentUnit;

    public string? ExpanderIconKey => HasChildren
        ? (IsExpanded ? ExpandedIconKey : CollapsedIconKey)
        : null;

    public double ExpanderOpacity => HasChildren ? 1d : 0d;
}
