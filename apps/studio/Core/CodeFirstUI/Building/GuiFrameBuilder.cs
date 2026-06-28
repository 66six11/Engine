using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Models;

namespace Editor.Core.CodeFirstUI;

public sealed class GuiFrameBuilder
{
    private readonly string panelId_;
    private readonly MutableGuiNode root_;
    private readonly Stack<MutableGuiNode> nodeStack_ = new();

    public GuiFrameBuilder(string panelId)
    {
        if (string.IsNullOrWhiteSpace(panelId))
        {
            throw new ArgumentException("Panel id must not be empty.", nameof(panelId));
        }

        panelId_ = panelId;
        root_ = new MutableGuiNode(
            new GuiNodeId(panelId_, string.Empty, GuiNodeKind.Root),
            GuiNodeKind.Root,
            Label: null,
            GuiNodePayload.None);
        nodeStack_.Push(root_);
    }

    public GuiNodeId Label(
        string key,
        string label,
        GuiTextTone tone = GuiTextTone.Secondary,
        GuiTextSize size = GuiTextSize.Body)
    {
        return AddLeaf(
            key,
            GuiNodeKind.Label,
            label,
            new GuiNodePayload
            {
                TextTone = tone,
                TextSize = size,
            });
    }

    public GuiNodeId Button(string key, string label)
    {
        return AddLeaf(key, GuiNodeKind.Button, label);
    }

    public GuiNodeId Separator(string key)
    {
        return AddLeaf(key, GuiNodeKind.Separator, label: null);
    }

    public GuiNodeId TextField(
        string key,
        string label,
        string text,
        GuiTextInputCommitMode commitMode = GuiTextInputCommitMode.OnLostFocus,
        TimeSpan? commitDelay = null)
    {
        if (commitDelay is { } delay && delay < TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(
                nameof(commitDelay),
                commitDelay,
                "Commit delay must not be negative.");
        }

        return AddLeaf(
            key,
            GuiNodeKind.TextField,
            label,
            new GuiNodePayload
            {
                TextValue = text,
                TextCommitMode = commitMode,
                TextCommitDelay = commitDelay,
            });
    }

    public GuiNodeId Toggle(
        string key,
        string label,
        bool isChecked)
    {
        return AddLeaf(
            key,
            GuiNodeKind.Toggle,
            label,
            new GuiNodePayload
            {
                IsChecked = isChecked,
            });
    }

    public GuiNodeId ComboBox(
        string key,
        string label,
        IReadOnlyList<GuiListItem> items,
        string? selectedItemId = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        return AddLeaf(
            key,
            GuiNodeKind.ComboBox,
            label,
            new GuiNodePayload
            {
                ListItems = items.ToArray(),
                SelectedItemId = selectedItemId,
            });
    }

    public GuiNodeId Slider(
        string key,
        string label,
        double value,
        double minimum = 0d,
        double maximum = 1d,
        double? smallChange = null,
        double? largeChange = null)
    {
        ValidateNumericRange(minimum, maximum);
        var clampedValue = ClampFinite(value, minimum, maximum, nameof(value));
        var range = maximum - minimum;

        return AddLeaf(
            key,
            GuiNodeKind.Slider,
            label,
            new GuiNodePayload
            {
                NumericValue = clampedValue,
                NumericMinimum = minimum,
                NumericMaximum = maximum,
                NumericSmallChange = ResolveNumericChange(
                    smallChange,
                    range / 100d,
                    nameof(smallChange)),
                NumericLargeChange = ResolveNumericChange(
                    largeChange,
                    range / 10d,
                    nameof(largeChange)),
            });
    }

    public GuiNodeId NumberInput(
        string key,
        string label,
        double value,
        double? minimum = null,
        double? maximum = null,
        double increment = 1d,
        string formatString = "0.###")
    {
        ValidateNumericBounds(minimum, maximum);
        var resolvedValue = ClampFiniteToBounds(value, minimum, maximum, nameof(value));
        var resolvedIncrement = ResolveNumericChange(
            increment,
            fallback: 1d,
            nameof(increment));
        if (string.IsNullOrWhiteSpace(formatString))
        {
            throw new ArgumentException("Format string must not be empty.", nameof(formatString));
        }

        return AddLeaf(
            key,
            GuiNodeKind.NumberInput,
            label,
            new GuiNodePayload
            {
                NumericValue = resolvedValue,
                NumericMinimum = minimum,
                NumericMaximum = maximum,
                NumericSmallChange = resolvedIncrement,
                NumericFormatString = formatString,
            });
    }

    public GuiNodeId ValidationMessage(
        string key,
        string message,
        EditorDiagnosticSeverity severity = EditorDiagnosticSeverity.Error)
    {
        return AddLeaf(
            key,
            GuiNodeKind.ValidationMessage,
            message,
            new GuiNodePayload
            {
                DiagnosticSeverity = severity,
            });
    }

    public GuiNodeId List(
        string key,
        IReadOnlyList<GuiListItem> items,
        string? selectedItemId = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        return AddLeaf(
            key,
            GuiNodeKind.List,
            label: null,
            new GuiNodePayload
            {
                ListItems = items.ToArray(),
                SelectedItemId = selectedItemId,
            });
    }

    public IDisposable Toolbar(string key)
    {
        return PushContainer(key, GuiNodeKind.Toolbar, label: null);
    }

    public IDisposable Horizontal(string key)
    {
        return PushContainer(key, GuiNodeKind.Horizontal, label: null);
    }

    public IDisposable Panel(string key, string label)
    {
        return PushContainer(key, GuiNodeKind.Panel, label);
    }

    public IDisposable NavigationView(
        string key,
        IReadOnlyList<GuiNavigationItem> items,
        string? selectedRoute,
        double ratio = 0.30d,
        IReadOnlyList<string>? collapsedNavigationRoutes = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (double.IsNaN(ratio) || double.IsInfinity(ratio) || ratio <= 0d || ratio >= 1d)
        {
            throw new ArgumentOutOfRangeException(
                nameof(ratio),
                ratio,
                "Navigation view ratio must be greater than 0 and less than 1.");
        }

        return PushContainer(
            key,
            GuiNodeKind.NavigationView,
            label: null,
            new GuiNodePayload
            {
                NavigationItems = items.ToArray(),
                SelectedRoute = selectedRoute,
                SplitRatio = ratio,
                CollapsedNavigationRoutes = collapsedNavigationRoutes?.ToArray() ?? [],
            });
    }

    public IDisposable Foldout(
        string key,
        string label,
        bool isExpanded)
    {
        return PushContainer(
            key,
            GuiNodeKind.Foldout,
            label,
            new GuiNodePayload
            {
                IsExpanded = isExpanded,
            });
    }

    public IDisposable Split(
        string key,
        GuiSplitDirection direction,
        double ratio)
    {
        if (double.IsNaN(ratio) || double.IsInfinity(ratio) || ratio <= 0d || ratio >= 1d)
        {
            throw new ArgumentOutOfRangeException(
                nameof(ratio),
                ratio,
                "Split ratio must be greater than 0 and less than 1.");
        }

        return PushContainer(
            key,
            GuiNodeKind.Split,
            label: null,
            new GuiNodePayload
            {
                SplitDirection = direction,
                SplitRatio = ratio,
            });
    }

    public IDisposable Scroll(string key)
    {
        return PushContainer(key, GuiNodeKind.Scroll, label: null);
    }

    public IDisposable Vertical(string key)
    {
        return PushContainer(key, GuiNodeKind.Vertical, label: null);
    }

    public GuiNodeId GetNodeId(string key, GuiNodeKind kind)
    {
        return CreateNodeId(key, kind);
    }

    public GuiTreeSnapshot Build()
    {
        return new GuiTreeSnapshot(panelId_, ToImmutable(root_));
    }

    private GuiNodeId AddLeaf(string key, GuiNodeKind kind, string? label)
    {
        return AddLeaf(key, kind, label, GuiNodePayload.None);
    }

    private GuiNodeId AddLeaf(
        string key,
        GuiNodeKind kind,
        string? label,
        GuiNodePayload payload)
    {
        var node = new MutableGuiNode(CreateNodeId(key, kind), kind, label, payload);
        nodeStack_.Peek().Children.Add(node);
        return node.Id;
    }

    private IDisposable PushContainer(string key, GuiNodeKind kind, string? label)
    {
        return PushContainer(key, kind, label, GuiNodePayload.None);
    }

    private IDisposable PushContainer(
        string key,
        GuiNodeKind kind,
        string? label,
        GuiNodePayload payload)
    {
        var node = new MutableGuiNode(CreateNodeId(key, kind), kind, label, payload);
        nodeStack_.Peek().Children.Add(node);
        nodeStack_.Push(node);
        return new GuiScope(this, node);
    }

    private GuiNodeId CreateNodeId(string key, GuiNodeKind kind)
    {
        if (string.IsNullOrWhiteSpace(key))
        {
            throw new ArgumentException("Node key must not be empty.", nameof(key));
        }

        var parentKeyPath = nodeStack_.Peek().Id.KeyPath;
        var keyPath = string.IsNullOrWhiteSpace(parentKeyPath)
            ? key
            : $"{parentKeyPath}/{key}";

        return new GuiNodeId(panelId_, keyPath, kind);
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

    private static double ResolveNumericChange(
        double? change,
        double fallback,
        string parameterName)
    {
        var resolved = change ?? fallback;
        if (double.IsNaN(resolved) || double.IsInfinity(resolved) || resolved <= 0d)
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                resolved,
                "Numeric change must be finite and greater than zero.");
        }

        return resolved;
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

    private void PopScope(MutableGuiNode expectedNode)
    {
        if (nodeStack_.Count <= 1 || !ReferenceEquals(nodeStack_.Peek(), expectedNode))
        {
            throw new InvalidOperationException("GUI scope disposal order does not match creation order.");
        }

        nodeStack_.Pop();
    }

    private static GuiNode ToImmutable(MutableGuiNode node)
    {
        return new GuiNode(
            node.Id,
            node.Kind,
            node.Label,
            node.Payload,
            node.Children.Select(ToImmutable).ToArray());
    }

    private sealed record MutableGuiNode(
        GuiNodeId Id,
        GuiNodeKind Kind,
        string? Label,
        GuiNodePayload Payload)
    {
        public List<MutableGuiNode> Children { get; } = [];
    }

    private sealed class GuiScope : IDisposable
    {
        private readonly GuiFrameBuilder builder_;
        private readonly MutableGuiNode node_;
        private bool isDisposed_;

        public GuiScope(GuiFrameBuilder builder, MutableGuiNode node)
        {
            builder_ = builder;
            node_ = node;
        }

        public void Dispose()
        {
            if (isDisposed_)
            {
                return;
            }

            builder_.PopScope(node_);
            isDisposed_ = true;
        }
    }
}
