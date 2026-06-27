using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Lucide.Avalonia;

namespace Editor.Shell.Icons;

public sealed class EditorIconRegistry
{
    private const double DefaultIconSize = 14d;
    private const double DefaultStrokeWidth = 2d;
    private readonly Dictionary<string, LucideIconKind> lucideIconsByKey_ = new(StringComparer.Ordinal);

    public static EditorIconRegistry Default { get; } = CreateDefault();

    public void RegisterLucide(string key, LucideIconKind kind)
    {
        if (string.IsNullOrWhiteSpace(key))
        {
            throw new ArgumentException("Icon key must not be empty.", nameof(key));
        }

        lucideIconsByKey_[key] = kind;
    }

    public bool ContainsIcon(string? key)
    {
        return !string.IsNullOrWhiteSpace(key)
            && lucideIconsByKey_.ContainsKey(key);
    }

    internal bool TryGetLucideKind(string? key, out LucideIconKind kind)
    {
        if (!string.IsNullOrWhiteSpace(key)
            && lucideIconsByKey_.TryGetValue(key, out kind))
        {
            return true;
        }

        kind = default;
        return false;
    }

    public Control? CreateIcon(
        string? key,
        double size = DefaultIconSize,
        double strokeWidth = DefaultStrokeWidth,
        IBrush? iconBrush = null)
    {
        if (string.IsNullOrWhiteSpace(key)
            || !lucideIconsByKey_.TryGetValue(key, out var kind))
        {
            return null;
        }

        return new LucideIcon
        {
            Kind = kind,
            Size = NormalizePositive(size, DefaultIconSize),
            StrokeWidth = NormalizePositive(strokeWidth, DefaultStrokeWidth),
            Foreground = iconBrush,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
        };
    }

    public Control CreateRequiredIcon(
        string key,
        double size = DefaultIconSize,
        double strokeWidth = DefaultStrokeWidth,
        IBrush? iconBrush = null)
    {
        return CreateIcon(key, size, strokeWidth, iconBrush)
            ?? throw new InvalidOperationException($"Editor icon '{key}' is not registered.");
    }

    private static EditorIconRegistry CreateDefault()
    {
        var registry = new EditorIconRegistry();
        registry.RegisterLucide(EditorIconKey.UiCheck, LucideIconKind.Check);
        registry.RegisterLucide(EditorIconKey.UiChevronDown, LucideIconKind.ChevronDown);
        registry.RegisterLucide(EditorIconKey.UiChevronRight, LucideIconKind.ChevronRight);
        registry.RegisterLucide(EditorIconKey.UiClose, LucideIconKind.X);
        registry.RegisterLucide(EditorIconKey.UiSearch, LucideIconKind.Search);
        registry.RegisterLucide(EditorIconKey.PanelSceneView, LucideIconKind.Box);
        registry.RegisterLucide(EditorIconKey.PanelHierarchy, LucideIconKind.ListTree);
        registry.RegisterLucide(EditorIconKey.PanelInspector, LucideIconKind.SlidersHorizontal);
        registry.RegisterLucide(EditorIconKey.PanelConsole, LucideIconKind.Terminal);
        registry.RegisterLucide(EditorIconKey.PanelProblems, LucideIconKind.CircleAlert);
        registry.RegisterLucide(EditorIconKey.PanelUiStyle, LucideIconKind.Palette);
        registry.RegisterLucide(EditorIconKey.ObjectDefault, LucideIconKind.Box);
        return registry;
    }

    private static double NormalizePositive(double value, double fallback)
    {
        return double.IsNaN(value) || double.IsInfinity(value) || value <= 0d
            ? fallback
            : value;
    }
}
