using System.Linq;
using Editor.Core.Models;
using Editor.Features.Inspector.Models;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Selection;
using Xunit;

namespace Editor.Tests.Features.Inspector;

public sealed class InspectorPanelViewModelTests
{
    [Fact]
    public void Constructor_starts_without_document_when_selection_is_empty()
    {
        var viewModel = new InspectorPanelViewModel(new EditorSelectionService());

        Assert.False(viewModel.HasDocument);
        Assert.Null(viewModel.Document);
    }

    [Fact]
    public void Constructor_builds_document_from_existing_selection()
    {
        var selectionService = new EditorSelectionService();
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("entity:1", "scene-object", "Cube", "studio.scene-object")]);

        var viewModel = new InspectorPanelViewModel(selectionService);

        AssertSingleSelectionDocument(viewModel.Document, includesIcon: true);
    }

    [Fact]
    public void Selection_changed_builds_single_selection_document()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService);

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("entity:1", "scene-object", "Cube", "studio.scene-object")]);

        Assert.True(viewModel.HasDocument);
        AssertSingleSelectionDocument(viewModel.Document, includesIcon: true);
    }

    [Fact]
    public void Clear_selection_removes_document()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService);
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("entity:1", "scene-object", "Cube")]);

        selectionService.ClearSelection("hierarchy");

        Assert.False(viewModel.HasDocument);
        Assert.Null(viewModel.Document);
    }

    [Fact]
    public void Multi_selection_builds_summary_document_without_merging_properties()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService);

        selectionService.ReplaceSelection(
            "hierarchy",
            [
                new EditorSelectionItem("entity:1", "scene-object", "Cube"),
                new EditorSelectionItem("entity:2", "scene-object", "Light"),
            ]);

        Assert.NotNull(viewModel.Document);
        var document = viewModel.Document!;
        Assert.True(document.IsMultiSelection);
        Assert.Equal(2, document.SelectionCount);
        Assert.Equal("2 items selected", document.Title);
        Assert.Equal("Multi-selection", document.Subtitle);
        var section = Assert.Single(document.Sections);
        Assert.Equal("Selection", section.Title);
        Assert.Equal(
            [
                new InspectorPropertyModel("Count", "2", InspectorPropertyValueKind.Count),
                new InspectorPropertyModel("Context", "hierarchy"),
            ],
            section.Properties);
    }

    [Fact]
    public void Replacing_same_selection_keeps_existing_document_instance()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService);
        var item = new EditorSelectionItem("entity:1", "scene-object", "Cube");
        selectionService.ReplaceSelection("hierarchy", [item]);
        var firstDocument = viewModel.Document;

        selectionService.ReplaceSelection("hierarchy", [item]);

        Assert.Same(firstDocument, viewModel.Document);
    }

    [Fact]
    public void Dispose_unsubscribes_from_selection_changes()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService);
        viewModel.Dispose();

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("entity:1", "scene-object", "Cube")]);

        Assert.Null(viewModel.Document);
    }

    private static void AssertSingleSelectionDocument(
        InspectorDocumentModel? document,
        bool includesIcon)
    {
        Assert.NotNull(document);
        var actual = document!;
        Assert.False(actual.IsMultiSelection);
        Assert.Equal(1, actual.SelectionCount);
        Assert.Equal("Cube", actual.Title);
        Assert.Equal("scene-object", actual.Subtitle);
        var section = Assert.Single(actual.Sections);
        Assert.Equal("Selection", section.Title);
        Assert.Contains(new InspectorPropertyModel("Name", "Cube"), section.Properties);
        Assert.Contains(new InspectorPropertyModel("Kind", "scene-object"), section.Properties);
        Assert.Contains(new InspectorPropertyModel("Id", "entity:1"), section.Properties);
        Assert.Contains(new InspectorPropertyModel("Context", "hierarchy"), section.Properties);
        Assert.Equal(
            includesIcon,
            section.Properties.Contains(new InspectorPropertyModel("Icon", "studio.scene-object")));
    }
}
