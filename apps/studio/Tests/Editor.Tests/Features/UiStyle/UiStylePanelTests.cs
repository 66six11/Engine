using Editor.Core.CodeFirstUI;
using Editor.Core.Models;
using Editor.Features.UiStyle;
using Editor.Shell.CodeFirstUI;
using Xunit;

namespace Editor.Tests.Features.UiStyle;

public sealed class UiStylePanelTests
{
    [Fact]
    public void Attach_builds_directory_and_overview_page()
    {
        var host = CreateAttachedHost();

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal(GuiNodeKind.Split, split.Kind);
        Assert.Equal(GuiSplitDirection.Horizontal, split.Payload.SplitDirection);

        var catalog = split.Children[0];
        Assert.Equal("Catalog", catalog.Label);
        var sections = Assert.Single(catalog.Children);
        Assert.Equal(GuiNodeKind.List, sections.Kind);
        Assert.Equal("overview", sections.Payload.SelectedItemId);
        Assert.Contains(sections.Payload.ListItems, item => item.Id == "buttons" && item.Label == "Buttons");
        Assert.Contains(sections.Payload.ListItems, item => item.Id == "foldouts" && item.Label == "Foldouts");

        var preview = split.Children[1];
        Assert.Equal("Preview", preview.Label);
        Assert.Equal("Overview", preview.Children[0].Label);
    }

    [Fact]
    public void Section_selection_rebuilds_corresponding_preview_page()
    {
        var host = CreateAttachedHost();

        host.SelectListItem(new GuiNodeId("ui-style", "layout/catalog/sections", GuiNodeKind.List), "buttons");

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        var preview = split.Children[1];
        Assert.Equal("Buttons", preview.Children[0].Label);
        Assert.Equal("buttons", split.Children[0].Children[0].Payload.SelectedItemId);
    }

    [Fact]
    public void Typography_page_contains_tone_and_size_samples()
    {
        var host = CreateAttachedHost();

        host.SelectListItem(new GuiNodeId("ui-style", "layout/catalog/sections", GuiNodeKind.List), "typography");

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        var preview = split.Children[1];
        var title = preview.Children[0];
        Assert.Equal("Typography", title.Label);
        Assert.Equal(GuiTextTone.Primary, title.Payload.TextTone);
        Assert.Equal(GuiTextSize.Title, title.Payload.TextSize);

        var primary = Assert.Single(preview.Children, child => child.Id.KeyPath == "layout/preview/primary");
        Assert.Equal(GuiTextTone.Primary, primary.Payload.TextTone);
        Assert.Equal(GuiTextSize.Body, primary.Payload.TextSize);

        var muted = Assert.Single(preview.Children, child => child.Id.KeyPath == "layout/preview/muted");
        Assert.Equal(GuiTextTone.Muted, muted.Payload.TextTone);
        Assert.Equal(GuiTextSize.Caption, muted.Payload.TextSize);
    }

    [Fact]
    public void Inputs_page_contains_code_first_text_field_and_toggle_samples()
    {
        var host = CreateAttachedHost();

        host.SelectListItem(new GuiNodeId("ui-style", "layout/catalog/sections", GuiNodeKind.List), "inputs");

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        var preview = split.Children[1];
        Assert.Equal("Inputs", preview.Children[0].Label);
        Assert.Contains(preview.Children, child => child.Kind == GuiNodeKind.TextField);
        Assert.Contains(preview.Children, child => child.Kind == GuiNodeKind.Toggle);
        var filter = Assert.Single(preview.Children, child => child.Kind == GuiNodeKind.TextField);
        Assert.Equal(GuiTextInputCommitMode.Debounced, filter.Payload.TextCommitMode);
    }

    [Fact]
    public void States_page_contains_scrollable_validation_feedback_samples()
    {
        var host = CreateAttachedHost();

        host.SelectListItem(new GuiNodeId("ui-style", "layout/catalog/sections", GuiNodeKind.List), "states");

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        var preview = split.Children[1];
        Assert.Equal("States", preview.Children[0].Label);
        var scroll = Assert.Single(preview.Children, child => child.Kind == GuiNodeKind.Scroll);
        Assert.Contains(
            scroll.Children,
            child => child.Kind == GuiNodeKind.ValidationMessage
                && child.Payload.DiagnosticSeverity == EditorDiagnosticSeverity.Warning);
        Assert.Contains(
            scroll.Children,
            child => child.Kind == GuiNodeKind.ValidationMessage
                && child.Payload.DiagnosticSeverity == EditorDiagnosticSeverity.Error);
    }

    [Fact]
    public void Foldouts_page_contains_expanded_and_collapsed_samples()
    {
        var host = CreateAttachedHost();

        host.SelectListItem(new GuiNodeId("ui-style", "layout/catalog/sections", GuiNodeKind.List), "foldouts");

        var split = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        var preview = split.Children[1];
        Assert.Equal("Foldouts", preview.Children[0].Label);
        var expanded = Assert.Single(preview.Children, child => child.Id.KeyPath == "layout/preview/rendering");
        Assert.Equal(GuiNodeKind.Foldout, expanded.Kind);
        Assert.True(expanded.Payload.IsExpanded);
        Assert.NotEmpty(expanded.Children);

        var collapsed = Assert.Single(preview.Children, child => child.Id.KeyPath == "layout/preview/advanced");
        Assert.Equal(GuiNodeKind.Foldout, collapsed.Kind);
        Assert.False(collapsed.Payload.IsExpanded);
        Assert.Empty(collapsed.Children);
    }

    private static CodeFirstPanelHostViewModel CreateAttachedHost()
    {
        var host = new CodeFirstPanelHostViewModel(new UiStylePanel());
        host.OnPanelAttached(new EditorPanelLifecycleContext(
            "ui-style",
            "UI Style",
            DockArea.Center,
            IsFloatingWorkspace: false));
        return host;
    }
}
