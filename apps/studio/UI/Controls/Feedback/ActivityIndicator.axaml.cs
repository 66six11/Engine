using Avalonia;
using Avalonia.Controls;

namespace Editor.UI.Controls.Feedback;

public partial class ActivityIndicator : UserControl
{
    public static readonly StyledProperty<bool> IsActiveProperty =
        AvaloniaProperty.Register<ActivityIndicator, bool>(nameof(IsActive));

    public static readonly StyledProperty<string?> TitleProperty =
        AvaloniaProperty.Register<ActivityIndicator, string?>(nameof(Title), string.Empty);

    public static readonly StyledProperty<string?> MessageProperty =
        AvaloniaProperty.Register<ActivityIndicator, string?>(nameof(Message), string.Empty);

    public ActivityIndicator()
    {
        InitializeComponent();
    }

    public bool IsActive
    {
        get => GetValue(IsActiveProperty);
        set => SetValue(IsActiveProperty, value);
    }

    public string? Title
    {
        get => GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public string? Message
    {
        get => GetValue(MessageProperty);
        set => SetValue(MessageProperty, value);
    }
}
