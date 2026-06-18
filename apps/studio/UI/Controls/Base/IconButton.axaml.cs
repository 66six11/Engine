using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Metadata;

namespace Editor.UI.Controls.Base;

[PseudoClasses(":selected", ":active")]
public class IconButton : Button
{
    public static readonly StyledProperty<string?> IconKeyProperty =
        AvaloniaProperty.Register<IconButton, string?>(nameof(IconKey));

    public static readonly StyledProperty<double> IconSizeProperty =
        AvaloniaProperty.Register<IconButton, double>(nameof(IconSize), 14d);

    public static readonly StyledProperty<double> StrokeWidthProperty =
        AvaloniaProperty.Register<IconButton, double>(nameof(StrokeWidth), 2d);

    public static readonly StyledProperty<bool> IsSelectedProperty =
        AvaloniaProperty.Register<IconButton, bool>(nameof(IsSelected));

    public static readonly StyledProperty<bool> IsActiveProperty =
        AvaloniaProperty.Register<IconButton, bool>(nameof(IsActive));

    public IconButton()
    {
        UpdatePseudoClasses();
    }

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

    public bool IsSelected
    {
        get => GetValue(IsSelectedProperty);
        set => SetValue(IsSelectedProperty, value);
    }

    public bool IsActive
    {
        get => GetValue(IsActiveProperty);
        set => SetValue(IsActiveProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == IsSelectedProperty
            || change.Property == IsActiveProperty)
        {
            UpdatePseudoClasses();
        }
    }

    private void UpdatePseudoClasses()
    {
        PseudoClasses.Set(":selected", IsSelected);
        PseudoClasses.Set(":active", IsActive);
    }
}
