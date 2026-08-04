using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;

namespace Editor.UI.Icons;

public sealed class EditorIconRegistry
{
    private const double DefaultIconSize = 14d;
    private readonly Dictionary<string, string> glyphsByKey_ = new(StringComparer.Ordinal);

    public static EditorIconRegistry Default { get; } = CreateDefault();

    public void RegisterGlyph(string key, string glyph)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        ArgumentException.ThrowIfNullOrWhiteSpace(glyph);
        glyphsByKey_[key] = glyph;
    }

    public bool ContainsIcon(string? key) =>
        !string.IsNullOrWhiteSpace(key) && glyphsByKey_.ContainsKey(key);

    public Control? CreateIcon(
        string? key,
        double size = DefaultIconSize,
        double strokeWidth = 2d,
        IBrush? iconBrush = null)
    {
        _ = strokeWidth;
        if (string.IsNullOrWhiteSpace(key) || !glyphsByKey_.TryGetValue(key, out var glyph))
        {
            return null;
        }
        var normalizedSize = double.IsFinite(size) && size > 0 ? size : DefaultIconSize;
        return new TextBlock
        {
            Text = glyph,
            FontSize = normalizedSize,
            Foreground = iconBrush,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
        };
    }

    public Control CreateRequiredIcon(
        string key,
        double size = DefaultIconSize,
        double strokeWidth = 2d,
        IBrush? iconBrush = null) =>
        CreateIcon(key, size, strokeWidth, iconBrush)
        ?? throw new InvalidOperationException($"Editor icon '{key}' is not registered.");

    private static EditorIconRegistry CreateDefault()
    {
        var registry = new EditorIconRegistry();
        registry.RegisterGlyph(EditorIconKey.UiCheck, "✓");
        registry.RegisterGlyph(EditorIconKey.UiChevronDown, "⌄");
        registry.RegisterGlyph(EditorIconKey.UiChevronRight, "›");
        registry.RegisterGlyph(EditorIconKey.UiClose, "×");
        registry.RegisterGlyph(EditorIconKey.UiSearch, "⌕");
        registry.RegisterGlyph(EditorIconKey.PanelSceneView, "◇");
        registry.RegisterGlyph(EditorIconKey.PanelHierarchy, "≡");
        registry.RegisterGlyph(EditorIconKey.PanelProject, "▣");
        registry.RegisterGlyph(EditorIconKey.PanelInspector, "☷");
        registry.RegisterGlyph(EditorIconKey.PanelConsole, ">_");
        registry.RegisterGlyph(EditorIconKey.PanelProblems, "!");
        registry.RegisterGlyph(EditorIconKey.PanelUiStyle, "◉");
        registry.RegisterGlyph(EditorIconKey.ObjectDefault, "◇");
        return registry;
    }
}
