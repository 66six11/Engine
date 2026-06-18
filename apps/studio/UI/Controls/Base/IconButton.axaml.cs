using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;

namespace Editor.UI.Controls.Base;

public partial class IconButton : UserControl
{
    public static readonly StyledProperty<string?> IconKeyProperty =
        AvaloniaProperty.Register<IconButton, string?>(nameof(IconKey));

    public static readonly StyledProperty<double> IconSizeProperty =
        AvaloniaProperty.Register<IconButton, double>(nameof(IconSize), 14d);

    public static readonly StyledProperty<double> StrokeWidthProperty =
        AvaloniaProperty.Register<IconButton, double>(nameof(StrokeWidth), 2d);

    public static readonly StyledProperty<ICommand?> CommandProperty =
        AvaloniaProperty.Register<IconButton, ICommand?>(nameof(Command));

    public static readonly StyledProperty<object?> CommandParameterProperty =
        AvaloniaProperty.Register<IconButton, object?>(nameof(CommandParameter));

    public static readonly StyledProperty<IconButtonVariant> VariantProperty =
        AvaloniaProperty.Register<IconButton, IconButtonVariant>(nameof(Variant));

    public static readonly StyledProperty<bool> IsSelectedProperty =
        AvaloniaProperty.Register<IconButton, bool>(nameof(IsSelected));

    public static readonly StyledProperty<bool> IsActiveProperty =
        AvaloniaProperty.Register<IconButton, bool>(nameof(IsActive));

    public IconButton()
    {
        InitializeComponent();
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

    public ICommand? Command
    {
        get => GetValue(CommandProperty);
        set => SetValue(CommandProperty, value);
    }

    public object? CommandParameter
    {
        get => GetValue(CommandParameterProperty);
        set => SetValue(CommandParameterProperty, value);
    }

    public IconButtonVariant Variant
    {
        get => GetValue(VariantProperty);
        set => SetValue(VariantProperty, value);
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
        if (change.Property == VariantProperty
            || change.Property == IsSelectedProperty
            || change.Property == IsActiveProperty)
        {
            UpdatePseudoClasses();
        }
    }

    private void UpdatePseudoClasses()
    {
        PseudoClasses.Set(":subtle", Variant == IconButtonVariant.Subtle);
        PseudoClasses.Set(":bare", Variant == IconButtonVariant.Bare);
        PseudoClasses.Set(":solid", Variant == IconButtonVariant.Solid);
        PseudoClasses.Set(":selected", IsSelected);
        PseudoClasses.Set(":active", IsActive);
    }
}
