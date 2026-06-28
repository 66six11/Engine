using System;
using Editor.Core.CodeFirstUI;
using Editor.Core.Models;

namespace Editor.Features.UiStyle;

internal sealed class UiStylePanel : CodeFirstEditorPanel
{
    private const string DefaultSectionId = "overview";

    private static readonly GuiNavigationPage[] Pages =
    [
        new("overview", "Overview", DrawOverviewPage),
        new("foundations/typography", "Typography", DrawTypographyPage),
        new("foundations/navigation", "Navigation", DrawNavigationPage),
        new("controls/buttons", "Buttons", DrawButtonsPage),
        new("controls/inputs", "Inputs", DrawInputsPage),
        new("controls/lists", "Lists", DrawListsPage),
        new("controls/foldouts", "Foldouts", DrawFoldoutsPage),
        new("feedback/states", "States", DrawStatesPage),
    ];

    private static readonly GuiListItem[] RenderModeOptions =
    [
        new("forward", "Forward"),
        new("deferred", "Deferred"),
        new("raytracing", "Ray Tracing"),
    ];

    private static readonly GuiListItem[] ShadingModeOptions =
    [
        new("lit", "Lit"),
        new("unlit", "Unlit"),
        new("wireframe", "Wireframe"),
    ];

    protected override void OnGui(EditorGui gui)
    {
        using (var navigation = gui.NavigationView("catalog", Pages, DefaultSectionId))
        {
            if (!navigation.DrawSelected(gui))
            {
                DrawOverviewPage(gui);
            }
        }
    }

    private static void DrawOverviewPage(EditorGui gui)
    {
        gui.Text("title", "Overview", GuiTextTone.Primary, GuiTextSize.Title);
        gui.Text("summary", "Code-first UI style samples for editor panels and tool windows.");
        gui.Text("layout", "The catalog uses a left navigation list and the preview uses the selected page.");
    }

    private static void DrawTypographyPage(EditorGui gui)
    {
        gui.Text("title", "Typography", GuiTextTone.Primary, GuiTextSize.Title);
        gui.Text("primary", "Primary text: compact labels, property names, and active values.", GuiTextTone.Primary);
        gui.Text("secondary", "Secondary text: descriptions, metadata, and low-emphasis hints.", GuiTextTone.Secondary);
        gui.Text("muted", "Muted caption: disabled, unavailable, or secondary diagnostic context.", GuiTextTone.Muted, GuiTextSize.Caption);
    }

    private static void DrawNavigationPage(EditorGui gui)
    {
        gui.Text("title", "Navigation", GuiTextTone.Primary, GuiTextSize.Title);
        gui.Text("routes", "Route paths use slash-separated segments for the left directory.");
        gui.Text("state", "Selected route and split ratio stay in GuiStateStore as panel-local UI state.");
        gui.Text("generator", "Source generators can emit page registries later; runtime reflection is not required.");
    }

    private static void DrawButtonsPage(EditorGui gui)
    {
        gui.Text("title", "Buttons", GuiTextTone.Primary, GuiTextSize.Title);
        using (gui.Toolbar("actions"))
        {
            gui.Button("primary", "Primary");
            gui.Button("secondary", "Secondary");
            gui.Button("danger", "Danger");
        }

        gui.Text("note", "Button clicks flow through the Code-first event queue before the next rebuild.");
    }

    private static void DrawInputsPage(EditorGui gui)
    {
        gui.Text("title", "Inputs", GuiTextTone.Primary, GuiTextSize.Title);
        gui.TextInput(
            "filter",
            "Filter",
            "material",
            GuiTextInputCommitMode.Debounced,
            TimeSpan.FromMilliseconds(250));
        gui.ComboBox("render-mode", "Render Mode", RenderModeOptions, "deferred");
        gui.RadioGroup("shading-mode", "Shading", ShadingModeOptions, "lit");
        gui.Slider("exposure", "Exposure", 0.75d, 0d, 2d, 0.05d, 0.25d);
        gui.NumberInput("roughness", "Roughness", 0.50d, 0d, 1d, 0.05d, "0.00");
        gui.Toggle("show-disabled", "Show Disabled");
        gui.Text("contract", "Input state belongs to GuiStateStore; document mutations should still go through commands.");
    }

    private static void DrawListsPage(EditorGui gui)
    {
        gui.Text("title", "Lists", GuiTextTone.Primary, GuiTextSize.Title);
        gui.Text("single", "Single-selection lists are stateful and rebuild the preview after selection changes.");
        gui.Text("future", "Tree, table, and virtualized collection nodes should share the same node/state boundary.");
    }

    private static void DrawFoldoutsPage(EditorGui gui)
    {
        gui.Text("title", "Foldouts", GuiTextTone.Primary, GuiTextSize.Title);
        using (var foldout = gui.Foldout("rendering", "Rendering Diagnostics"))
        {
            if (foldout.IsExpanded)
            {
                gui.Text("capture", "Capture state, pass metadata, and shader diagnostics belong under focused foldouts.");
                gui.ValidationMessage(
                    "warning",
                    "Warning: collapsed foldouts should skip expensive child UI declaration.",
                    EditorDiagnosticSeverity.Warning);
            }
        }

        using (var foldout = gui.Foldout("advanced", "Advanced", defaultExpanded: false))
        {
            if (foldout.IsExpanded)
            {
                gui.Text("deferred", "This deferred sample is only declared when the foldout is expanded.");
            }
        }
    }

    private static void DrawStatesPage(EditorGui gui)
    {
        gui.Text("title", "States", GuiTextTone.Primary, GuiTextSize.Title);
        using (gui.Scroll("feedback"))
        {
            gui.ProgressBar(
                "shader-import",
                "Shader Import",
                42d,
                0d,
                100d,
                showProgressText: true,
                progressTextFormat: "{1:0}%");
            gui.ValidationMessage(
                "info",
                "Info: editable and command-ready.",
                EditorDiagnosticSeverity.Info);
            gui.ValidationMessage(
                "warning",
                "Warning: visible but missing optional metadata.",
                EditorDiagnosticSeverity.Warning);
            gui.ValidationMessage(
                "error",
                "Error: validation should report through diagnostics instead of silent UI failure.",
                EditorDiagnosticSeverity.Error);
        }
    }
}
