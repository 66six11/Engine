using Avalonia;
using Avalonia.Controls;

namespace Editor.Shell.Views.Docking;

public partial class EditorDockWindowSurfaceView : UserControl
{
    public static readonly StyledProperty<object?> TabStripContentProperty =
        AvaloniaProperty.Register<EditorDockWindowSurfaceView, object?>(nameof(TabStripContent));

    public static readonly StyledProperty<object?> BodyContentProperty =
        AvaloniaProperty.Register<EditorDockWindowSurfaceView, object?>(nameof(BodyContent));

    public static readonly StyledProperty<bool> IsActiveWindowProperty =
        AvaloniaProperty.Register<EditorDockWindowSurfaceView, bool>(nameof(IsActiveWindow));

    public static readonly StyledProperty<bool> IsDragSourceWindowProperty =
        AvaloniaProperty.Register<EditorDockWindowSurfaceView, bool>(nameof(IsDragSourceWindow));

    public static readonly StyledProperty<bool> IsPreviewProperty =
        AvaloniaProperty.Register<EditorDockWindowSurfaceView, bool>(nameof(IsPreview));

    public EditorDockWindowSurfaceView()
    {
        InitializeComponent();
    }

    public object? TabStripContent
    {
        get => GetValue(TabStripContentProperty);
        set => SetValue(TabStripContentProperty, value);
    }

    public object? BodyContent
    {
        get => GetValue(BodyContentProperty);
        set => SetValue(BodyContentProperty, value);
    }

    public bool IsActiveWindow
    {
        get => GetValue(IsActiveWindowProperty);
        set => SetValue(IsActiveWindowProperty, value);
    }

    public bool IsDragSourceWindow
    {
        get => GetValue(IsDragSourceWindowProperty);
        set => SetValue(IsDragSourceWindowProperty, value);
    }

    public bool IsPreview
    {
        get => GetValue(IsPreviewProperty);
        set => SetValue(IsPreviewProperty, value);
    }
}
