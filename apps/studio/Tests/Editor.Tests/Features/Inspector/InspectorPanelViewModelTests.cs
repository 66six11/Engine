using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Abstractions;
using Editor.Core.Models.Scene;
using Editor.Core.Models.Selection;
using Editor.Core.Services;
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
        var viewModel = new InspectorPanelViewModel(new EditorSelectionService(), CreateProvider());

        Assert.False(viewModel.HasDocument);
        Assert.Null(viewModel.Document);
    }

    [Fact]
    public void Constructor_builds_document_from_existing_provider_selection()
    {
        var selectionService = new EditorSelectionService();
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube", "studio.object.default")]);

        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());

        AssertSingleSelectionDocument(viewModel.Document);
    }

    [Fact]
    public void Selection_changed_builds_single_selection_document_from_provider()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube", "studio.object.default")]);

        Assert.True(viewModel.HasDocument);
        AssertSingleSelectionDocument(viewModel.Document);
    }

    [Fact]
    public void Missing_selection_builds_validation_document()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/missing", "mesh", "Missing Cube")]);

        Assert.NotNull(viewModel.Document);
        var document = viewModel.Document!;
        Assert.Equal("Missing Cube", document.Title);
        Assert.Equal("Missing selection", document.Subtitle);
        var section = Assert.Single(document.Sections);
        Assert.Equal("Validation", section.Title);
        Assert.Contains(new InspectorPropertyModel("State", "Missing"), section.Properties);
        Assert.Contains(new InspectorPropertyModel("Id", "scene:test/missing"), section.Properties);
        Assert.Contains(new InspectorPropertyModel("Context", "hierarchy"), section.Properties);
    }

    [Fact]
    public void Clear_selection_removes_document()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube")]);

        selectionService.ClearSelection("hierarchy");

        Assert.False(viewModel.HasDocument);
        Assert.Null(viewModel.Document);
    }

    [Fact]
    public void Multi_selection_builds_summary_document_without_merging_properties()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());

        selectionService.ReplaceSelection(
            "hierarchy",
            [
                new EditorSelectionItem("scene:test/cube", "mesh", "Cube"),
                new EditorSelectionItem("scene:test/light", "light", "Light"),
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
    public void Snapshot_changed_rebuilds_current_selection_document()
    {
        var selectionService = new EditorSelectionService();
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube")]);
        var provider = CreateProvider();
        var viewModel = new InspectorPanelViewModel(
            selectionService,
            provider,
            new CapturingUiDispatcher(hasAccess: true));

        provider.ReplaceSnapshot(new SceneSnapshot(
            "scene:test",
            "Runtime Snapshot",
            2,
            [
                new SceneObjectSnapshot(
                    "scene:test/cube",
                    "Runtime Cube",
                    "mesh",
                    properties:
                    [
                        new SceneObjectPropertySnapshot("triangles", "Triangles", "24", SceneObjectPropertyValueKind.Count),
                    ]),
            ]));

        Assert.NotNull(viewModel.Document);
        var document = viewModel.Document!;
        Assert.Equal("Runtime Cube", document.Title);
        var propertySection = Assert.Single(document.Sections, section => section.Title == "Properties");
        Assert.Equal(
            [new InspectorPropertyModel("Triangles", "24", InspectorPropertyValueKind.Count)],
            propertySection.Properties);
    }

    [Fact]
    public void Snapshot_changed_posts_document_refresh_when_dispatcher_has_no_access()
    {
        var selectionService = new EditorSelectionService();
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube")]);
        var provider = CreateProvider();
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var viewModel = new InspectorPanelViewModel(selectionService, provider, dispatcher);

        provider.ReplaceSnapshot(new SceneSnapshot(
            "scene:test",
            "Runtime Snapshot",
            2,
            [new SceneObjectSnapshot("scene:test/cube", "Runtime Cube", "mesh")]));

        Assert.Equal("Cube", viewModel.Document?.Title);
        var action = Assert.Single(dispatcher.PostedActions);

        action();

        Assert.Equal("Runtime Cube", viewModel.Document?.Title);
    }

    [Fact]
    public void Replacing_same_selection_keeps_existing_document_instance()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());
        var item = new EditorSelectionItem("scene:test/cube", "mesh", "Cube");
        selectionService.ReplaceSelection("hierarchy", [item]);
        var firstDocument = viewModel.Document;

        selectionService.ReplaceSelection("hierarchy", [item]);

        Assert.Same(firstDocument, viewModel.Document);
    }

    [Fact]
    public void Dispose_unsubscribes_from_snapshot_changes()
    {
        var selectionService = new EditorSelectionService();
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube")]);
        var provider = CreateProvider();
        var viewModel = new InspectorPanelViewModel(selectionService, provider);
        viewModel.Dispose();

        provider.ReplaceSnapshot(new SceneSnapshot(
            "scene:test",
            "Runtime Snapshot",
            2,
            [new SceneObjectSnapshot("scene:test/cube", "Runtime Cube", "mesh")]));

        Assert.Equal("Cube", viewModel.Document?.Title);
    }

    [Fact]
    public void Dispose_unsubscribes_from_selection_changes()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());
        viewModel.Dispose();

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube")]);

        Assert.Null(viewModel.Document);
    }

    private static InMemorySceneSnapshotProvider CreateProvider()
    {
        return new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:test",
            "Test Scene",
            1,
            [
                new SceneObjectSnapshot(
                    "scene:test/cube",
                    "Cube",
                    "mesh",
                    parentId: "scene:test",
                    iconKey: "studio.object.default",
                    properties:
                    [
                        new SceneObjectPropertySnapshot("triangles", "Triangles", "12", SceneObjectPropertyValueKind.Count),
                    ]),
                new SceneObjectSnapshot("scene:test/light", "Light", "light", parentId: "scene:test"),
            ]));
    }

    private static void AssertSingleSelectionDocument(InspectorDocumentModel? document)
    {
        Assert.NotNull(document);
        var actual = document;
        Assert.False(actual.IsMultiSelection);
        Assert.Equal(1, actual.SelectionCount);
        Assert.Equal("Cube", actual.Title);
        Assert.Equal("mesh", actual.Subtitle);
        Assert.Equal(["Selection", "Properties"], actual.Sections.Select(section => section.Title));
        var selectionSection = actual.Sections[0];
        Assert.Contains(new InspectorPropertyModel("Name", "Cube"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Kind", "mesh"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Id", "scene:test/cube"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Active", "True"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Parent", "scene:test"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Icon", "studio.object.default"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Context", "hierarchy"), selectionSection.Properties);

        var propertySection = actual.Sections[1];
        Assert.Equal(
            [new InspectorPropertyModel("Triangles", "12", InspectorPropertyValueKind.Count)],
            propertySection.Properties);
    }

    private sealed class CapturingUiDispatcher(bool hasAccess) : IEditorUiDispatcher
    {
        public List<Action> PostedActions { get; } = [];

        public bool CheckAccess() => hasAccess;

        public void Post(Action action)
        {
            PostedActions.Add(action);
        }
    }
}
