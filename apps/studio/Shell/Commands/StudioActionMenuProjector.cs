using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using Asharia.Studio.Application.Actions;
using Avalonia.Controls;
using Editor.Shell.ViewModels.Windowing;

namespace Editor.Shell.Commands;

internal static class StudioActionMenuProjector
{
    public static IReadOnlyList<MenuItem> ProjectTopLevelMenus(
        StudioShellViewModel shell,
        StudioPresentationId topLevelId,
        StudioPresentationId? focusedPanelId)
    {
        ArgumentNullException.ThrowIfNull(shell);
        return Project(
            shell,
            context: null,
            StudioActionPlacementKind.Menu,
            pathRoot: null,
            topLevelId,
            focusedPanelId);
    }

    public static IReadOnlyList<MenuItem> ProjectContextMenu(
        StudioShellViewModel shell,
        StudioActionContextSnapshot context,
        string pathRoot)
    {
        ArgumentNullException.ThrowIfNull(shell);
        ArgumentNullException.ThrowIfNull(context);
        ArgumentException.ThrowIfNullOrWhiteSpace(pathRoot);
        return Project(
            shell,
            context,
            StudioActionPlacementKind.ContextMenu,
            pathRoot,
            context.TopLevelId,
            context.FocusedPanelId);
    }

    private static IReadOnlyList<MenuItem> Project(
        StudioShellViewModel shell,
        StudioActionContextSnapshot? context,
        StudioActionPlacementKind kind,
        string? pathRoot,
        StudioPresentationId? topLevelId,
        StudioPresentationId? focusedPanelId)
    {
        var leaves = shell.ActionCatalog
            .SelectMany(entry => entry.Placements
                .Where(placement => placement.Kind == kind)
                .Select(placement => new ProjectedLeaf(entry, placement)))
            .Where(leaf => TryGetRelativeSegments(
                leaf.Placement.Path,
                pathRoot,
                out _))
            .ToArray();
        if (leaves.Length == 0)
        {
            return [];
        }

        var root = new ProjectedMenuNode(string.Empty);
        foreach (var leaf in leaves)
        {
            _ = TryGetRelativeSegments(leaf.Placement.Path, pathRoot, out var segments);
            var node = root;
            for (var index = 0; index < segments.Length - 1; index++)
            {
                node = node.GetOrAdd(segments[index]);
            }
            node.Leaves.Add(leaf);
        }

        AssignBranchOrder(root);

        var items = new List<object?>();
        foreach (var child in root.Children.Values
                     .OrderBy(child => child.Order)
                     .ThenBy(child => child.Header, StringComparer.Ordinal))
        {
            items.Add(CreateBranch(shell, context, child, topLevelId, focusedPanelId));
        }
        AppendLeaves(items, shell, context, root.Leaves, topLevelId, focusedPanelId);
        return items.OfType<MenuItem>().ToArray();
    }

    private static MenuItem CreateBranch(
        StudioShellViewModel shell,
        StudioActionContextSnapshot? context,
        ProjectedMenuNode node,
        StudioPresentationId? topLevelId,
        StudioPresentationId? focusedPanelId)
    {
        var menuItem = new MenuItem
        {
            Header = node.Header,
        };
        foreach (var child in node.Children.Values
                     .OrderBy(child => child.Order)
                     .ThenBy(child => child.Header, StringComparer.Ordinal))
        {
            menuItem.Items.Add(CreateBranch(
                shell,
                context,
                child,
                topLevelId,
                focusedPanelId));
        }
        AppendLeaves(
            menuItem.Items,
            shell,
            context,
            node.Leaves,
            topLevelId,
            focusedPanelId);
        return menuItem;
    }

    private static void AppendLeaves(
        IList items,
        StudioShellViewModel shell,
        StudioActionContextSnapshot? context,
        IReadOnlyList<ProjectedLeaf> leaves,
        StudioPresentationId? topLevelId,
        StudioPresentationId? focusedPanelId)
    {
        string? previousSection = null;
        foreach (var leaf in leaves
                     .OrderBy(leaf => leaf.Placement.Order)
                     .ThenBy(leaf => leaf.Entry.Definition.Id.Value,
                         StringComparer.Ordinal))
        {
            var definition = leaf.Entry.Definition;
            var actionContext = context ?? shell.CaptureActionContext(
                StudioActionInvocationSource.Menu,
                topLevelId,
                focusedPanelId: focusedPanelId);
            var actionState = shell.EvaluateAction(definition.Id, actionContext);
            if (actionState.Status == StudioActionStateEvaluationStatus.Evaluated &&
                actionState.State is { IsVisible: false })
            {
                continue;
            }
            if (previousSection is not null &&
                !string.Equals(previousSection, leaf.Placement.Section,
                    StringComparison.Ordinal))
            {
                items.Add(new Separator());
            }

            var state = actionState.Status == StudioActionStateEvaluationStatus.Evaluated
                ? actionState.State
                : null;
            var item = new MenuItem
            {
                Header = state?.PresentationLabel ?? definition.Label,
                Command = context is null
                    ? shell.GetActionCommand(
                        definition.Id,
                        StudioActionInvocationSource.Menu,
                        topLevelId!.Value,
                        focusedPanelId)
                    : shell.GetActionCommand(definition.Id),
                CommandParameter = context,
                Tag = definition.Id.Value,
                ToggleType = state?.CheckState == StudioActionCheckState.NotCheckable ||
                             state is null
                    ? MenuItemToggleType.None
                    : MenuItemToggleType.CheckBox,
                IsChecked = state?.IsChecked ?? false,
            };
            items.Add(item);
            previousSection = leaf.Placement.Section;
        }
    }

    private static bool TryGetRelativeSegments(
        string? path,
        string? pathRoot,
        out string[] segments)
    {
        segments = path?.Split(
            '/',
            StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries) ?? [];
        if (segments.Length < 2)
        {
            return false;
        }
        if (pathRoot is null)
        {
            return true;
        }
        if (!string.Equals(segments[0], pathRoot, StringComparison.Ordinal))
        {
            segments = [];
            return false;
        }

        segments = segments[1..];
        return segments.Length != 0;
    }

    private static int AssignBranchOrder(ProjectedMenuNode node)
    {
        var order = node.Leaves.Count == 0
            ? int.MaxValue
            : node.Leaves.Min(leaf => leaf.Placement.Order);
        foreach (var child in node.Children.Values)
        {
            order = Math.Min(order, AssignBranchOrder(child));
        }
        node.Order = order;
        return order;
    }

    private sealed record ProjectedLeaf(
        StudioActionCatalogEntry Entry,
        StudioActionPlacement Placement);

    private sealed class ProjectedMenuNode(string header)
    {
        public string Header { get; } = header;

        public int Order { get; set; } = int.MaxValue;

        public Dictionary<string, ProjectedMenuNode> Children { get; } =
            new(StringComparer.Ordinal);

        public List<ProjectedLeaf> Leaves { get; } = [];

        public ProjectedMenuNode GetOrAdd(string childHeader)
        {
            if (!Children.TryGetValue(childHeader, out var child))
            {
                child = new ProjectedMenuNode(childHeader);
                Children.Add(childHeader, child);
            }
            return child;
        }
    }
}
