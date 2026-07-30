using System;
using System.Linq;
using Asharia.Editor.Panels;
using Xunit;

namespace Asharia.Editor.Tests.Panels;

public sealed class EditorPanelSinkContractTests
{
    [Fact]
    public void Panel_sink_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(IEditorPanelFrameUpdateSink),
            typeof(IEditorPanelLayoutSink),
            typeof(IEditorPanelLifecycleSink),
            typeof(IEditorPanelVisibilitySink),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Panels", type.Namespace));
    }

    [Fact]
    public void Existing_panel_lifecycle_contract_remains_unchanged()
    {
        var methods = typeof(IEditorPanelLifecycleSink)
            .GetMethods()
            .Select(method => method.Name)
            .OrderBy(name => name, StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(
            [
                nameof(IEditorPanelLifecycleSink.OnPanelActivated),
                nameof(IEditorPanelLifecycleSink.OnPanelAttached),
                nameof(IEditorPanelLifecycleSink.OnPanelDeactivated),
                nameof(IEditorPanelLifecycleSink.OnPanelDetached),
            ],
            methods);
    }

    [Fact]
    public void Panel_layout_context_preserves_logical_geometry_and_scale()
    {
        var panel = new EditorPanelLifecycleContext(
            "scene",
            "Scene",
            EditorDockArea.Center,
            IsFloatingWorkspace: false);

        var context = new EditorPanelLayoutContext(
            panel,
            logicalWidth: 1280,
            logicalHeight: 720,
            renderScale: 1.5);

        Assert.Same(panel, context.Panel);
        Assert.Equal(1280, context.LogicalWidth);
        Assert.Equal(720, context.LogicalHeight);
        Assert.Equal(1.5, context.RenderScale);
        Assert.True(context.HasPositiveArea);
    }

    [Theory]
    [InlineData(-1, 1, 1)]
    [InlineData(double.NaN, 1, 1)]
    [InlineData(1, -1, 1)]
    [InlineData(1, double.PositiveInfinity, 1)]
    [InlineData(1, 1, 0)]
    [InlineData(1, 1, double.NaN)]
    public void Panel_layout_context_rejects_invalid_geometry(
        double logicalWidth,
        double logicalHeight,
        double renderScale)
    {
        var panel = new EditorPanelLifecycleContext(
            "scene",
            "Scene",
            EditorDockArea.Center,
            IsFloatingWorkspace: false);

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new EditorPanelLayoutContext(
                panel,
                logicalWidth,
                logicalHeight,
                renderScale));
    }
}
