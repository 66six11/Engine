using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Interactivity;

namespace Editor.UI.Controls.Base;

public partial class SearchBox : UserControl
{
    public static readonly StyledProperty<string?> TextProperty =
        AvaloniaProperty.Register<SearchBox, string?>(
            nameof(Text),
            string.Empty,
            defaultBindingMode: BindingMode.TwoWay);

    public static readonly StyledProperty<string?> PlaceholderTextProperty =
        AvaloniaProperty.Register<SearchBox, string?>(nameof(PlaceholderText));

    public static readonly StyledProperty<bool> IsSearchFocusedProperty =
        AvaloniaProperty.Register<SearchBox, bool>(nameof(IsSearchFocused));

    public SearchBox()
    {
        InitializeComponent();
        SearchTextBox.GotFocus += OnSearchTextBoxFocusChanged;
        SearchTextBox.LostFocus += OnSearchTextBoxFocusChanged;
    }

    public string? Text
    {
        get => GetValue(TextProperty);
        set => SetValue(TextProperty, value);
    }

    public string? PlaceholderText
    {
        get => GetValue(PlaceholderTextProperty);
        set => SetValue(PlaceholderTextProperty, value);
    }

    public bool IsSearchFocused
    {
        get => GetValue(IsSearchFocusedProperty);
        private set => SetValue(IsSearchFocusedProperty, value);
    }

    public void FocusSearchText()
    {
        SearchTextBox.Focus();
        SearchTextBox.SelectAll();
    }

    private void OnSearchTextBoxFocusChanged(object? sender, RoutedEventArgs e)
    {
        IsSearchFocused = SearchTextBox.IsKeyboardFocusWithin;
    }
}
