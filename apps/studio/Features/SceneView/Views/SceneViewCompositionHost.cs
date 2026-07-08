using Avalonia;
using Avalonia.Controls;
using Avalonia.Rendering.Composition;
using Avalonia.VisualTree;

namespace Editor.Features.SceneView.Views;

internal sealed class SceneViewCompositionHost : Control
{
    private CompositionSurfaceVisual? visual_;
    private CompositionDrawingSurface? surface_;

    public CompositionDrawingSurface? Surface => surface_;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        AttachCompositionVisual();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        ElementComposition.SetElementChildVisual(this, null);
        visual_ = null;
        surface_?.Dispose();
        surface_ = null;
        base.OnDetachedFromVisualTree(e);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        if (change.Property == BoundsProperty && visual_ is not null)
        {
            visual_.Size = CurrentVisualSize();
        }

        base.OnPropertyChanged(change);
    }

    private void AttachCompositionVisual()
    {
        if (visual_ is not null)
        {
            return;
        }

        var selfVisual = ElementComposition.GetElementVisual(this);
        if (selfVisual is null)
        {
            return;
        }

        var compositor = selfVisual.Compositor;
        surface_ = compositor.CreateDrawingSurface();
        visual_ = compositor.CreateSurfaceVisual();
        visual_.Size = CurrentVisualSize();
        visual_.Surface = surface_;
        ElementComposition.SetElementChildVisual(this, visual_);
    }

    private Vector CurrentVisualSize()
    {
        return new Vector(Bounds.Width, Bounds.Height);
    }
}
