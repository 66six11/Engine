using Avalonia;
using Avalonia.Controls;
using Editor.Features.SceneView.Interop;
using Editor.Features.SceneView.ViewModels;

namespace Editor.Features.SceneView.Views;

public partial class SceneViewPanelView : UserControl
{
    private readonly SceneViewCompositionCapabilityReader compositionReader_ = new();

    public SceneViewPanelView()
    {
        InitializeComponent();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        ProbeCompositionCapabilities();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == BoundsProperty)
        {
            ProbeCompositionCapabilities();
        }
    }

    private async void ProbeCompositionCapabilities()
    {
        if (DataContext is not SceneViewPanelViewModel viewModel)
        {
            return;
        }

        var snapshot = await compositionReader_.ReadAsync(this, viewModel.ViewportId);
        viewModel.UpdateCompositionCapabilities(snapshot);
    }
}
