using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace Editor.Shell.Icons;

public sealed class EditorIconView : ContentControl
{
    public static readonly StyledProperty<string?> IconKeyProperty =
        AvaloniaProperty.Register<EditorIconView, string?>(nameof(IconKey));

    public static readonly StyledProperty<double> IconSizeProperty =
        AvaloniaProperty.Register<EditorIconView, double>(nameof(IconSize), 14d);

    public static readonly StyledProperty<double> StrokeWidthProperty =
        AvaloniaProperty.Register<EditorIconView, double>(nameof(StrokeWidth), 2d);

    public static readonly StyledProperty<IBrush?> IconBrushProperty =
        AvaloniaProperty.Register<EditorIconView, IBrush?>(nameof(IconBrush));

    public string? IconKey
    {
        get => GetValue(IconKeyProperty);
        set => SetValue(IconKeyProperty, value);
    }

    public double IconSize
    {
        get => GetValue(IconSizeProperty);
        set => SetValue(IconSizeProperty, value);
    }

    public double StrokeWidth
    {
        get => GetValue(StrokeWidthProperty);
        set => SetValue(StrokeWidthProperty, value);
    }

    public IBrush? IconBrush
    {
        get => GetValue(IconBrushProperty);
        set => SetValue(IconBrushProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == IconKeyProperty
            || change.Property == IconSizeProperty
            || change.Property == StrokeWidthProperty
            || change.Property == IconBrushProperty)
        {
            RefreshIcon();
        }
    }

    private void RefreshIcon()
    {
        var icon = EditorIconRegistry.Default.CreateIcon(IconKey, IconSize, StrokeWidth, IconBrush);
        Content = icon;
        IsVisible = icon is not null;
    }
}
