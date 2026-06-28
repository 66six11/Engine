using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Models;

namespace Editor.Core.CodeFirstUI;

public sealed class EditorGui
{
    private readonly GuiFrameBuilder builder_;
    private readonly IEditorGuiCommandExecutor commandExecutor_;
    private readonly GuiEventQueue events_;

    public EditorGui(
        GuiFrameBuilder builder,
        GuiEventQueue events,
        GuiStateStore stateStore,
        IEditorGuiCommandExecutor commandExecutor)
    {
        ArgumentNullException.ThrowIfNull(builder);
        ArgumentNullException.ThrowIfNull(events);
        ArgumentNullException.ThrowIfNull(stateStore);
        ArgumentNullException.ThrowIfNull(commandExecutor);

        builder_ = builder;
        events_ = events;
        StateStore = stateStore;
        commandExecutor_ = commandExecutor;
    }

    public GuiStateStore StateStore { get; }

    public void Label(
        string key,
        string label,
        GuiTextTone tone = GuiTextTone.Secondary,
        GuiTextSize size = GuiTextSize.Body)
    {
        builder_.Label(key, label, tone, size);
    }

    public void Text(
        string key,
        string text,
        GuiTextTone tone = GuiTextTone.Secondary,
        GuiTextSize size = GuiTextSize.Body)
    {
        Label(key, text, tone, size);
    }

    public bool Button(string key, string label)
    {
        var nodeId = builder_.Button(key, label);
        return events_.ConsumeButtonClicked(nodeId);
    }

    public void Separator(string key)
    {
        builder_.Separator(key);
    }

    public string TextInput(
        string key,
        string label,
        string text = "",
        GuiTextInputCommitMode commitMode = GuiTextInputCommitMode.OnLostFocus,
        TimeSpan? commitDelay = null)
    {
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.TextField);
        var resolvedText = StateStore.TryGetText(nodeId, out var storedText)
            ? storedText ?? string.Empty
            : text;
        StateStore.SetText(nodeId, resolvedText);
        builder_.TextField(key, label, resolvedText, commitMode, commitDelay);
        return resolvedText;
    }

    public bool Toggle(
        string key,
        string label,
        bool isChecked = false)
    {
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Toggle);
        var resolvedValue = StateStore.TryGetToggle(nodeId, out var storedValue)
            ? storedValue
            : isChecked;
        StateStore.SetToggle(nodeId, resolvedValue);
        builder_.Toggle(key, label, resolvedValue);
        return resolvedValue;
    }

    public string? ComboBox(
        string key,
        string label,
        IReadOnlyList<GuiListItem> items,
        string? selectedItemId = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.ComboBox);
        var selected = ResolveItemSelection(nodeId, items, selectedItemId);
        if (selected is not null)
        {
            StateStore.SetSelectedItem(nodeId, selected);
        }

        builder_.ComboBox(key, label, items, selected);
        return selected;
    }

    public double Slider(
        string key,
        string label,
        double value,
        double minimum = 0d,
        double maximum = 1d,
        double? smallChange = null,
        double? largeChange = null)
    {
        ValidateNumericRange(minimum, maximum);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Slider);
        var resolvedValue = StateStore.TryGetNumericValue(nodeId, out var storedValue)
            ? storedValue
            : value;
        resolvedValue = ClampFinite(resolvedValue, minimum, maximum, nameof(value));
        StateStore.SetNumericValue(nodeId, resolvedValue);
        builder_.Slider(
            key,
            label,
            resolvedValue,
            minimum,
            maximum,
            smallChange,
            largeChange);
        return resolvedValue;
    }

    public double NumberInput(
        string key,
        string label,
        double value,
        double? minimum = null,
        double? maximum = null,
        double increment = 1d,
        string formatString = "0.###")
    {
        ValidateNumericBounds(minimum, maximum);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.NumberInput);
        var resolvedValue = StateStore.TryGetNumericValue(nodeId, out var storedValue)
            ? storedValue
            : value;
        resolvedValue = ClampFiniteToBounds(resolvedValue, minimum, maximum, nameof(value));
        StateStore.SetNumericValue(nodeId, resolvedValue);
        builder_.NumberInput(
            key,
            label,
            resolvedValue,
            minimum,
            maximum,
            increment,
            formatString);
        return resolvedValue;
    }

    public void ValidationMessage(
        string key,
        string message,
        EditorDiagnosticSeverity severity = EditorDiagnosticSeverity.Error)
    {
        builder_.ValidationMessage(key, message, severity);
    }

    public string? List(
        string key,
        IReadOnlyList<GuiListItem> items,
        string? selectedItemId = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.List);
        var selected = ResolveItemSelection(nodeId, items, selectedItemId);
        if (selected is not null)
        {
            StateStore.SetSelectedItem(nodeId, selected);
        }

        builder_.List(key, items, selected);
        return selected;
    }

    public GuiNavigationScope NavigationView(
        string key,
        IReadOnlyList<GuiNavigationPage> pages,
        string defaultRoute,
        double ratio = 0.30d)
    {
        ArgumentNullException.ThrowIfNull(pages);

        var items = pages.Select(page => page.Item).ToArray();
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.NavigationView);
        var selectedRoute = ResolveNavigationRoute(nodeId, items, defaultRoute);
        if (selectedRoute is not null)
        {
            StateStore.SetSelectedRoute(nodeId, selectedRoute);
        }

        var resolvedRatio = StateStore.TryGetSplitRatio(nodeId, out var storedRatio)
            ? storedRatio
            : ratio;
        var collapsedRoutes = ResolveCollapsedNavigationRoutes(nodeId, items);

        return new GuiNavigationScope(
            builder_.NavigationView(key, items, selectedRoute, resolvedRatio, collapsedRoutes),
            selectedRoute,
            pages);
    }

    public WorkbenchCommandExecutionResult? CommandButton(
        string key,
        string label,
        string commandId)
    {
        return Button(key, label)
            ? ExecuteCommand(commandId)
            : null;
    }

    public WorkbenchCommandExecutionResult ExecuteCommand(string commandId)
    {
        return commandExecutor_.Execute(commandId);
    }

    public IDisposable Toolbar(string key)
    {
        return builder_.Toolbar(key);
    }

    public IDisposable Horizontal(string key)
    {
        return builder_.Horizontal(key);
    }

    public IDisposable Panel(string key, string label)
    {
        return builder_.Panel(key, label);
    }

    public GuiFoldoutScope Foldout(
        string key,
        string label,
        bool defaultExpanded = true)
    {
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Foldout);
        var isExpanded = StateStore.TryGetFoldoutExpanded(nodeId, out var storedExpanded)
            ? storedExpanded
            : defaultExpanded;
        StateStore.SetFoldoutExpanded(nodeId, isExpanded);
        return new GuiFoldoutScope(
            builder_.Foldout(key, label, isExpanded),
            isExpanded);
    }

    public IDisposable Split(
        string key,
        GuiSplitDirection direction,
        double ratio)
    {
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Split);
        var resolvedRatio = StateStore.TryGetSplitRatio(nodeId, out var storedRatio)
            ? storedRatio
            : ratio;
        return builder_.Split(key, direction, resolvedRatio);
    }

    public IDisposable Scroll(string key)
    {
        return builder_.Scroll(key);
    }

    public IDisposable Vertical(string key)
    {
        return builder_.Vertical(key);
    }

    private string? ResolveItemSelection(
        GuiNodeId nodeId,
        IReadOnlyList<GuiListItem> items,
        string? selectedItemId)
    {
        if (items.Count == 0)
        {
            return null;
        }

        if (StateStore.TryGetSelectedItem(nodeId, out var storedSelection)
            && ContainsItem(items, storedSelection))
        {
            return storedSelection;
        }

        if (ContainsItem(items, selectedItemId))
        {
            return selectedItemId;
        }

        return items[0].Id;
    }

    private string? ResolveNavigationRoute(
        GuiNodeId nodeId,
        IReadOnlyList<GuiNavigationItem> items,
        string? defaultRoute)
    {
        if (items.Count == 0)
        {
            return null;
        }

        if (StateStore.TryGetSelectedRoute(nodeId, out var storedRoute)
            && ContainsNavigationRoute(items, storedRoute))
        {
            return storedRoute;
        }

        if (ContainsNavigationRoute(items, defaultRoute))
        {
            return defaultRoute;
        }

        return items[0].Route;
    }

    private IReadOnlyList<string> ResolveCollapsedNavigationRoutes(
        GuiNodeId nodeId,
        IReadOnlyList<GuiNavigationItem> items)
    {
        var collapsedRoutes = new List<string>();
        foreach (var route in EnumerateNavigationRoutePrefixes(items))
        {
            if (StateStore.TryGetNavigationRouteExpanded(nodeId, route, out var isExpanded)
                && !isExpanded)
            {
                collapsedRoutes.Add(route);
            }
        }

        return collapsedRoutes;
    }

    private static IReadOnlyList<string> EnumerateNavigationRoutePrefixes(
        IReadOnlyList<GuiNavigationItem> items)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var routes = new List<string>();
        foreach (var item in items)
        {
            var route = string.Empty;
            foreach (var segment in item.Route.Split('/'))
            {
                route = string.IsNullOrWhiteSpace(route)
                    ? segment
                    : $"{route}/{segment}";
                if (seen.Add(route))
                {
                    routes.Add(route);
                }
            }
        }

        return routes;
    }

    private static bool ContainsNavigationRoute(
        IReadOnlyList<GuiNavigationItem> items,
        string? route)
    {
        return !string.IsNullOrWhiteSpace(route)
            && items.Any(item => string.Equals(item.Route, route, StringComparison.Ordinal));
    }

    private static bool ContainsItem(
        IReadOnlyList<GuiListItem> items,
        string? itemId)
    {
        return !string.IsNullOrWhiteSpace(itemId)
            && items.Any(item => string.Equals(item.Id, itemId, StringComparison.Ordinal));
    }

    private static void ValidateNumericRange(double minimum, double maximum)
    {
        if (double.IsNaN(minimum) || double.IsInfinity(minimum))
        {
            throw new ArgumentOutOfRangeException(
                nameof(minimum),
                minimum,
                "Minimum must be finite.");
        }

        if (double.IsNaN(maximum) || double.IsInfinity(maximum) || maximum <= minimum)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maximum),
                maximum,
                "Maximum must be finite and greater than minimum.");
        }
    }

    private static double ClampFinite(
        double value,
        double minimum,
        double maximum,
        string parameterName)
    {
        if (double.IsNaN(value) || double.IsInfinity(value))
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                value,
                "Value must be finite.");
        }

        return Math.Clamp(value, minimum, maximum);
    }

    private static void ValidateNumericBounds(double? minimum, double? maximum)
    {
        if (minimum is { } min && (double.IsNaN(min) || double.IsInfinity(min)))
        {
            throw new ArgumentOutOfRangeException(
                nameof(minimum),
                minimum,
                "Minimum must be finite.");
        }

        if (maximum is { } max && (double.IsNaN(max) || double.IsInfinity(max)))
        {
            throw new ArgumentOutOfRangeException(
                nameof(maximum),
                maximum,
                "Maximum must be finite.");
        }

        if (minimum is { } finiteMin && maximum is { } finiteMax && finiteMax <= finiteMin)
        {
            throw new ArgumentOutOfRangeException(
                nameof(maximum),
                maximum,
                "Maximum must be greater than minimum.");
        }
    }

    private static double ClampFiniteToBounds(
        double value,
        double? minimum,
        double? maximum,
        string parameterName)
    {
        if (double.IsNaN(value) || double.IsInfinity(value))
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                value,
                "Value must be finite.");
        }

        if (minimum is { } min && value < min)
        {
            return min;
        }

        if (maximum is { } max && value > max)
        {
            return max;
        }

        return value;
    }
}
