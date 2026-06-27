using System;
using Editor.Core.CodeFirstUI;

namespace Editor.Features.UiStyle;

internal sealed class UiStylePanel : CodeFirstEditorPanel
{
    private const string DefaultSectionId = "overview";

    private static readonly GuiListItem[] Sections =
    [
        new("overview", "Overview"),
        new("typography", "Typography"),
        new("buttons", "Buttons"),
        new("inputs", "Inputs"),
        new("lists", "Lists"),
        new("states", "States"),
    ];

    protected override void OnGui(EditorGui gui)
    {
        using (gui.Split("layout", GuiSplitDirection.Horizontal, 0.30d))
        {
            var selectedSectionId = DefaultSectionId;

            using (gui.Panel("catalog", "Catalog"))
            {
                selectedSectionId = gui.List("sections", Sections, DefaultSectionId) ?? DefaultSectionId;
            }

            using (gui.Panel("preview", "Preview"))
            {
                DrawPreviewPage(gui, selectedSectionId);
            }
        }
    }

    private static void DrawPreviewPage(EditorGui gui, string selectedSectionId)
    {
        switch (selectedSectionId)
        {
            case "typography":
                DrawTypographyPage(gui);
                break;
            case "buttons":
                DrawButtonsPage(gui);
                break;
            case "inputs":
                DrawInputsPage(gui);
                break;
            case "lists":
                DrawListsPage(gui);
                break;
            case "states":
                DrawStatesPage(gui);
                break;
            default:
                DrawOverviewPage(gui);
                break;
        }
    }

    private static void DrawOverviewPage(EditorGui gui)
    {
        gui.Text("title", "Overview");
        gui.Text("summary", "Code-first UI style samples for editor panels and tool windows.");
        gui.Text("layout", "The catalog uses a left navigation list and the preview uses the selected page.");
    }

    private static void DrawTypographyPage(EditorGui gui)
    {
        gui.Text("title", "Typography");
        gui.Text("primary", "Primary text: compact labels, property names, and active values.");
        gui.Text("secondary", "Secondary text: descriptions, metadata, and low-emphasis hints.");
        gui.Text("muted", "Muted text: disabled or unavailable editor state.");
    }

    private static void DrawButtonsPage(EditorGui gui)
    {
        gui.Text("title", "Buttons");
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
        gui.Text("title", "Inputs");
        gui.TextInput(
            "filter",
            "Filter",
            "material",
            GuiTextInputCommitMode.Debounced,
            TimeSpan.FromMilliseconds(250));
        gui.Toggle("show-disabled", "Show Disabled");
        gui.Text("contract", "Input state belongs to GuiStateStore; document mutations should still go through commands.");
    }

    private static void DrawListsPage(EditorGui gui)
    {
        gui.Text("title", "Lists");
        gui.Text("single", "Single-selection lists are stateful and rebuild the preview after selection changes.");
        gui.Text("future", "Tree, table, and virtualized collection nodes should share the same node/state boundary.");
    }

    private static void DrawStatesPage(EditorGui gui)
    {
        gui.Text("title", "States");
        gui.Text("normal", "Normal: editable and command-ready.");
        gui.Text("disabled", "Disabled: visible but unavailable.");
        gui.Text("error", "Error: validation should report through diagnostics instead of silent UI failure.");
    }
}
