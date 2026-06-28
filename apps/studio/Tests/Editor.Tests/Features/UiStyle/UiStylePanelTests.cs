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

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal(GuiNodeKind.NavigationView, navigation.Kind);
        Assert.Equal("overview", navigation.Payload.SelectedRoute);
        Assert.Contains(navigation.Payload.NavigationItems, item => item.Route == "foundations/navigation" && item.Label == "Navigation");
        Assert.Contains(navigation.Payload.NavigationItems, item => item.Route == "controls/buttons" && item.Label == "Buttons");
        Assert.Contains(navigation.Payload.NavigationItems, item => item.Route == "controls/foldouts" && item.Label == "Foldouts");
        Assert.Equal("Overview", navigation.Children[0].Label);
    }

    [Fact]
    public void Section_selection_rebuilds_corresponding_preview_page()
    {
        var host = CreateAttachedHost();

        host.SelectNavigationRoute(new GuiNodeId("ui-style", "catalog", GuiNodeKind.NavigationView), "controls/buttons");

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal("Buttons", navigation.Children[0].Label);
        Assert.Equal("controls/buttons", navigation.Payload.SelectedRoute);
    }

    [Fact]
    public void Typography_page_contains_tone_and_size_samples()
    {
        var host = CreateAttachedHost();

        host.SelectNavigationRoute(new GuiNodeId("ui-style", "catalog", GuiNodeKind.NavigationView), "foundations/typography");

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        var title = navigation.Children[0];
        Assert.Equal("Typography", title.Label);
        Assert.Equal(GuiTextTone.Primary, title.Payload.TextTone);
        Assert.Equal(GuiTextSize.Title, title.Payload.TextSize);

        var primary = Assert.Single(navigation.Children, child => child.Id.KeyPath == "catalog/primary");
        Assert.Equal(GuiTextTone.Primary, primary.Payload.TextTone);
        Assert.Equal(GuiTextSize.Body, primary.Payload.TextSize);

        var muted = Assert.Single(navigation.Children, child => child.Id.KeyPath == "catalog/muted");
        Assert.Equal(GuiTextTone.Muted, muted.Payload.TextTone);
        Assert.Equal(GuiTextSize.Caption, muted.Payload.TextSize);
    }

    [Fact]
    public void Navigation_page_documents_route_directory_contract()
    {
        var host = CreateAttachedHost();

        host.SelectNavigationRoute(new GuiNodeId("ui-style", "catalog", GuiNodeKind.NavigationView), "foundations/navigation");

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal("Navigation", navigation.Children[0].Label);
        Assert.Contains(navigation.Children, child => child.Label == "Route paths use slash-separated segments for the left directory.");
        Assert.Contains(navigation.Children, child => child.Label == "Selected route and split ratio stay in GuiStateStore as panel-local UI state.");
    }

    [Fact]
    public void Inputs_page_contains_code_first_text_field_toggle_combo_box_radio_slider_color_vector_and_number_input_samples()
    {
        var host = CreateAttachedHost();

        host.SelectNavigationRoute(new GuiNodeId("ui-style", "catalog", GuiNodeKind.NavigationView), "controls/inputs");

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal("Inputs", navigation.Children[0].Label);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.TextField);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.Toggle);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.ComboBox);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.RadioGroup);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.Slider);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.ColorField);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.Vector2Field);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.Vector3Field);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.Vector4Field);
        Assert.Contains(navigation.Children, child => child.Kind == GuiNodeKind.NumberInput);
        var filter = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.TextField);
        Assert.Equal(GuiTextInputCommitMode.Debounced, filter.Payload.TextCommitMode);
        var comboBox = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.ComboBox);
        Assert.Equal("deferred", comboBox.Payload.SelectedItemId);
        Assert.Contains(comboBox.Payload.ListItems, item => item.Id == "forward" && item.Label == "Forward");
        var radioGroup = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.RadioGroup);
        Assert.Equal("lit", radioGroup.Payload.SelectedItemId);
        Assert.Contains(radioGroup.Payload.ListItems, item => item.Id == "wireframe" && item.Label == "Wireframe");
        var slider = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.Slider);
        Assert.Equal(0.75d, slider.Payload.NumericValue);
        Assert.Equal(2d, slider.Payload.NumericMaximum);
        var colorField = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.ColorField);
        Assert.Equal(new GuiColorValue(128, 160, 255, 255), colorField.Payload.ColorValue);
        Assert.True(colorField.Payload.ShowAlpha);
        var vector2Field = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.Vector2Field);
        Assert.Equal(new GuiVector2Value(1d, 1d), vector2Field.Payload.Vector2Value);
        Assert.Equal(0d, vector2Field.Payload.NumericMinimum);
        Assert.Equal(8d, vector2Field.Payload.NumericMaximum);
        var vector3Field = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.Vector3Field);
        Assert.Equal(new GuiVector3Value(0d, 1d, 2d), vector3Field.Payload.Vector3Value);
        Assert.Equal(-100d, vector3Field.Payload.NumericMinimum);
        Assert.Equal(100d, vector3Field.Payload.NumericMaximum);
        var vector4Field = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.Vector4Field);
        Assert.Equal(new GuiVector4Value(1d, 1d, 0d, 0d), vector4Field.Payload.Vector4Value);
        Assert.Equal(-8d, vector4Field.Payload.NumericMinimum);
        Assert.Equal(8d, vector4Field.Payload.NumericMaximum);
        var numberInput = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.NumberInput);
        Assert.Equal(0.50d, numberInput.Payload.NumericValue);
        Assert.Equal("0.00", numberInput.Payload.NumericFormatString);
    }

    [Fact]
    public void States_page_contains_scrollable_validation_feedback_samples()
    {
        var host = CreateAttachedHost();

        host.SelectNavigationRoute(new GuiNodeId("ui-style", "catalog", GuiNodeKind.NavigationView), "feedback/states");

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal("States", navigation.Children[0].Label);
        var scroll = Assert.Single(navigation.Children, child => child.Kind == GuiNodeKind.Scroll);
        Assert.Contains(
            scroll.Children,
            child => child.Kind == GuiNodeKind.ValidationMessage
                && child.Payload.DiagnosticSeverity == EditorDiagnosticSeverity.Warning);
        Assert.Contains(
            scroll.Children,
            child => child.Kind == GuiNodeKind.ValidationMessage
                && child.Payload.DiagnosticSeverity == EditorDiagnosticSeverity.Error);
        var progressBar = Assert.Single(scroll.Children, child => child.Kind == GuiNodeKind.ProgressBar);
        Assert.Equal("Shader Import", progressBar.Label);
        Assert.Equal(42d, progressBar.Payload.NumericValue);
        Assert.Equal(100d, progressBar.Payload.NumericMaximum);
        Assert.True(progressBar.Payload.ShowProgressText);
    }

    [Fact]
    public void Foldouts_page_contains_expanded_and_collapsed_samples()
    {
        var host = CreateAttachedHost();

        host.SelectNavigationRoute(new GuiNodeId("ui-style", "catalog", GuiNodeKind.NavigationView), "controls/foldouts");

        var navigation = Assert.Single(host.CurrentTree?.Root.Children ?? []);
        Assert.Equal("Foldouts", navigation.Children[0].Label);
        var expanded = Assert.Single(navigation.Children, child => child.Id.KeyPath == "catalog/rendering");
        Assert.Equal(GuiNodeKind.Foldout, expanded.Kind);
        Assert.True(expanded.Payload.IsExpanded);
        Assert.NotEmpty(expanded.Children);

        var collapsed = Assert.Single(navigation.Children, child => child.Id.KeyPath == "catalog/advanced");
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
