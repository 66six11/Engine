using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
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

    public static readonly StyledProperty<bool> IsPlaceholderVisibleProperty =
        AvaloniaProperty.Register<SearchBox, bool>(nameof(IsPlaceholderVisible), true);

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

    public bool IsPlaceholderVisible
    {
        get => GetValue(IsPlaceholderVisibleProperty);
        private set => SetValue(IsPlaceholderVisibleProperty, value);
    }

    public void FocusSearchText()
    {
        SearchTextBox.Focus();
        SearchTextBox.SelectAll();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == TextProperty)
        {
            IsPlaceholderVisible = string.IsNullOrEmpty(Text);
        }
    }

    private void OnSearchTextBoxFocusChanged(object? sender, RoutedEventArgs e)
    {
        IsSearchFocused = SearchTextBox.IsKeyboardFocusWithin;
    }

    private void OnSearchBoxPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (!SearchTextBox.IsKeyboardFocusWithin)
        {
            SearchTextBox.Focus();
        }
    }
}
