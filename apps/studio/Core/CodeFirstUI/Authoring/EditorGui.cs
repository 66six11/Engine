using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Asharia.Editor.Commands;
using Asharia.Editor.Diagnostics;
using Editor.Core.Models.Workbench;
using Editor.Core.CodeFirstUI.Abstractions;
using Editor.Core.CodeFirstUI.Building;
using Editor.Core.CodeFirstUI.Events;
using Editor.Core.CodeFirstUI.Models;
using Editor.Core.CodeFirstUI.State;

namespace Editor.Core.CodeFirstUI.Authoring;

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

    public void Property(
        string key,
        string label,
        string value)
    {
        builder_.Property(key, label, value);
    }

    public void Property<T>(
        string key,
        string label,
        T value)
    {
        builder_.Property(key, label, value?.ToString() ?? string.Empty);
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

    public string? RadioGroup(
        string key,
        string label,
        IReadOnlyList<GuiListItem> items,
        string? selectedItemId = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.RadioGroup);
        var selected = ResolveItemSelection(nodeId, items, selectedItemId);
        if (selected is not null)
        {
            StateStore.SetSelectedItem(nodeId, selected);
        }

        builder_.RadioGroup(key, label, items, selected);
        return selected;
    }

    public TEnum EnumPopup<TEnum>(
        string key,
        string label,
        TEnum selected)
        where TEnum : struct, Enum
    {
        var items = EnumPopupCache<TEnum>.Items;
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.ComboBox);
        var selectedItemId = EnumPopupCache<TEnum>.GetItemId(selected);
        var resolvedItemId = ResolveItemSelection(nodeId, items, selectedItemId);
        if (resolvedItemId is not null)
        {
            StateStore.SetSelectedItem(nodeId, resolvedItemId);
        }

        builder_.ComboBox(key, label, items, resolvedItemId);
        return resolvedItemId is not null
            && EnumPopupCache<TEnum>.TryGetValue(resolvedItemId, out var resolvedValue)
                ? resolvedValue
                : selected;
    }

    public GuiColorValue ColorField(
        string key,
        string label,
        GuiColorValue value,
        bool showAlpha = true)
    {
        var nodeId = builder_.GetNodeId(key, GuiNodeKind.ColorField);
        var resolvedValue = StateStore.TryGetColorValue(nodeId, out var storedValue)
            ? storedValue
            : value;
        StateStore.SetColorValue(nodeId, resolvedValue);
        builder_.ColorField(key, label, resolvedValue, showAlpha);
        return resolvedValue;
    }

    public GuiVector3Value Vector3Field(
        string key,
        string label,
        GuiVector3Value value,
        double? minimum = null,
        double? maximum = null,
        double increment = 0.1d,
        string formatString = "0.###")
    {
        ValidateNumericBounds(minimum, maximum);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Vector3Field);
        var resolvedValue = StateStore.TryGetVector3Value(nodeId, out var storedValue)
            ? storedValue
            : value;
        resolvedValue = ClampVector3ToBounds(resolvedValue, minimum, maximum, nameof(value));
        StateStore.SetVector3Value(nodeId, resolvedValue);
        builder_.Vector3Field(
            key,
            label,
            resolvedValue,
            minimum,
            maximum,
            increment,
            formatString);
        return resolvedValue;
    }

    public GuiVector2Value Vector2Field(
        string key,
        string label,
        GuiVector2Value value,
        double? minimum = null,
        double? maximum = null,
        double increment = 0.1d,
        string formatString = "0.###")
    {
        ValidateNumericBounds(minimum, maximum);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Vector2Field);
        var resolvedValue = StateStore.TryGetVector2Value(nodeId, out var storedValue)
            ? storedValue
            : value;
        resolvedValue = ClampVector2ToBounds(resolvedValue, minimum, maximum, nameof(value));
        StateStore.SetVector2Value(nodeId, resolvedValue);
        builder_.Vector2Field(
            key,
            label,
            resolvedValue,
            minimum,
            maximum,
            increment,
            formatString);
        return resolvedValue;
    }

    public GuiVector4Value Vector4Field(
        string key,
        string label,
        GuiVector4Value value,
        double? minimum = null,
        double? maximum = null,
        double increment = 0.1d,
        string formatString = "0.###")
    {
        ValidateNumericBounds(minimum, maximum);

        var nodeId = builder_.GetNodeId(key, GuiNodeKind.Vector4Field);
        var resolvedValue = StateStore.TryGetVector4Value(nodeId, out var storedValue)
            ? storedValue
            : value;
        resolvedValue = ClampVector4ToBounds(resolvedValue, minimum, maximum, nameof(value));
        StateStore.SetVector4Value(nodeId, resolvedValue);
        builder_.Vector4Field(
            key,
            label,
            resolvedValue,
            minimum,
            maximum,
            increment,
            formatString);
        return resolvedValue;
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

    public float FloatField(
        string key,
        string label,
        float value,
        double? minimum = null,
        double? maximum = null,
        double increment = 0.1d,
        string formatString = "0.###")
    {
        var resolvedValue = NumberInput(
            key,
            label,
            value,
            minimum,
            maximum,
            increment,
            formatString);
        return (float)resolvedValue;
    }

    public void ProgressBar(
        string key,
        string label,
        double value,
        double minimum = 0d,
        double maximum = 100d,
        bool isIndeterminate = false,
        bool showProgressText = false,
        string? progressTextFormat = null)
    {
        builder_.ProgressBar(
            key,
            label,
            value,
            minimum,
            maximum,
            isIndeterminate,
            showProgressText,
            progressTextFormat);
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

    public EditorCommandExecutionResult? CommandButton(
        string key,
        string label,
        string commandId)
    {
        return Button(key, label)
            ? ExecuteCommand(commandId)
            : null;
    }

    public EditorCommandExecutionResult ExecuteCommand(string commandId)
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

    private static string FormatEnumLabel(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return string.Empty;
        }

        var builder = new StringBuilder(value.Length + 4);
        var previousWasLowerOrDigit = false;
        for (var index = 0; index < value.Length; index++)
        {
            var character = value[index];
            if (character is '_' or '-')
            {
                if (builder.Length > 0 && builder[^1] != ' ')
                {
                    builder.Append(' ');
                }

                previousWasLowerOrDigit = false;
                continue;
            }

            if (index > 0 && char.IsUpper(character) && previousWasLowerOrDigit)
            {
                builder.Append(' ');
            }

            builder.Append(character);
            previousWasLowerOrDigit = char.IsLower(character) || char.IsDigit(character);
        }

        return builder.ToString();
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

    private static GuiVector3Value ClampVector3ToBounds(
        GuiVector3Value value,
        double? minimum,
        double? maximum,
        string parameterName)
    {
        return new GuiVector3Value(
            ClampFiniteToBounds(value.X, minimum, maximum, parameterName),
            ClampFiniteToBounds(value.Y, minimum, maximum, parameterName),
            ClampFiniteToBounds(value.Z, minimum, maximum, parameterName));
    }

    private static GuiVector2Value ClampVector2ToBounds(
        GuiVector2Value value,
        double? minimum,
        double? maximum,
        string parameterName)
    {
        return new GuiVector2Value(
            ClampFiniteToBounds(value.X, minimum, maximum, parameterName),
            ClampFiniteToBounds(value.Y, minimum, maximum, parameterName));
    }

    private static GuiVector4Value ClampVector4ToBounds(
        GuiVector4Value value,
        double? minimum,
        double? maximum,
        string parameterName)
    {
        return new GuiVector4Value(
            ClampFiniteToBounds(value.X, minimum, maximum, parameterName),
            ClampFiniteToBounds(value.Y, minimum, maximum, parameterName),
            ClampFiniteToBounds(value.Z, minimum, maximum, parameterName),
            ClampFiniteToBounds(value.W, minimum, maximum, parameterName));
    }

    // ReSharper disable once StaticMemberInGenericType
    private static class EnumPopupCache<TEnum>
        where TEnum : struct, Enum
    {
        public static IReadOnlyList<GuiListItem> Items { get; } = CreateItems();

        private static IReadOnlyDictionary<string, TEnum> ValuesById { get; } = CreateValuesById();

        public static string GetItemId(TEnum value)
        {
            return value.ToString();
        }

        public static bool TryGetValue(string itemId, out TEnum value)
        {
            return ValuesById.TryGetValue(itemId, out value);
        }

        private static IReadOnlyList<GuiListItem> CreateItems()
        {
            return Enum.GetValues<TEnum>()
                .Select(value =>
                {
                    var id = GetItemId(value);
                    return new GuiListItem(id, FormatEnumLabel(id));
                })
                .ToArray();
        }

        private static IReadOnlyDictionary<string, TEnum> CreateValuesById()
        {
            var values = new Dictionary<string, TEnum>(StringComparer.Ordinal);
            foreach (var value in Enum.GetValues<TEnum>())
            {
                values.TryAdd(GetItemId(value), value);
            }

            return values;
        }
    }
}
