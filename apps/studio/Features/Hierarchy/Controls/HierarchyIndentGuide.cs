using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace Editor.Features.Hierarchy.Controls;

public sealed class HierarchyIndentGuide : Control
{
    public static readonly StyledProperty<int> DepthProperty =
        AvaloniaProperty.Register<HierarchyIndentGuide, int>(nameof(Depth));

    public static readonly StyledProperty<double> IndentUnitProperty =
        AvaloniaProperty.Register<HierarchyIndentGuide, double>(nameof(IndentUnit), 12d);

    public static readonly StyledProperty<IBrush?> LineBrushProperty =
        AvaloniaProperty.Register<HierarchyIndentGuide, IBrush?>(nameof(LineBrush));

    public static readonly StyledProperty<bool> IsLastSiblingProperty =
        AvaloniaProperty.Register<HierarchyIndentGuide, bool>(nameof(IsLastSibling));

    public static readonly StyledProperty<ulong> AncestorContinuationMaskProperty =
        AvaloniaProperty.Register<HierarchyIndentGuide, ulong>(nameof(AncestorContinuationMask));

    public int Depth
    {
        get => GetValue(DepthProperty);
        set => SetValue(DepthProperty, value);
    }

    public double IndentUnit
    {
        get => GetValue(IndentUnitProperty);
        set => SetValue(IndentUnitProperty, value);
    }

    public IBrush? LineBrush
    {
        get => GetValue(LineBrushProperty);
        set => SetValue(LineBrushProperty, value);
    }

    public bool IsLastSibling
    {
        get => GetValue(IsLastSiblingProperty);
        set => SetValue(IsLastSiblingProperty, value);
    }

    public ulong AncestorContinuationMask
    {
        get => GetValue(AncestorContinuationMaskProperty);
        set => SetValue(AncestorContinuationMaskProperty, value);
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);

        var depth = Depth;
        var indentUnit = IndentUnit;
        var brush = LineBrush;
        if (depth <= 0 || indentUnit <= 0d || brush is null)
        {
            return;
        }

        var width = Bounds.Width;
        var height = Bounds.Height;
        if (width <= 0d || height <= 0d)
        {
            return;
        }

        var pen = new Pen(brush);
        var centerY = Snap(height / 2d);
        var continuationMask = AncestorContinuationMask;
        var ancestorDepth = Math.Min(depth - 1, 64);
        for (var level = 0; level < ancestorDepth; level++)
        {
            if ((continuationMask & (1UL << level)) == 0UL)
            {
                continue;
            }

            var x = Snap((level * indentUnit) + (indentUnit / 2d));
            context.DrawLine(pen, new Point(x, 0d), new Point(x, height));
        }

        var connectorX = Snap(((depth - 1) * indentUnit) + (indentUnit / 2d));
        var verticalEndY = IsLastSibling ? centerY : height;
        context.DrawLine(pen, new Point(connectorX, 0d), new Point(connectorX, verticalEndY));
        context.DrawLine(pen, new Point(connectorX, centerY), new Point(width, centerY));
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == DepthProperty
            || change.Property == IndentUnitProperty
            || change.Property == LineBrushProperty
            || change.Property == IsLastSiblingProperty
            || change.Property == AncestorContinuationMaskProperty)
        {
            InvalidateVisual();
        }
    }

    private static double Snap(double value)
    {
        return double.Floor(value) + 0.5d;
    }
}
