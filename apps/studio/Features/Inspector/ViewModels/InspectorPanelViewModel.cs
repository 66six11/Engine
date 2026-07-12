using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Asharia.Editor.Threading;
using Editor.Core.Abstractions;
using Asharia.Editor.Worlds.Snapshots;
using Asharia.Editor.Selection;
using Editor.Core.Services;
using Editor.Features.Inspector.Models;
using Editor.UI.ViewModels;

namespace Editor.Features.Inspector.ViewModels;

public sealed class InspectorPanelViewModel : ViewModelBase, IDisposable
{
    private readonly IEditorSelectionService selectionService_;
    private readonly ISceneSnapshotProvider sceneSnapshotProvider_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private EditorSelectionSnapshot currentSelection_;
    private InspectorDocumentModel? document_;

    public InspectorPanelViewModel(IEditorSelectionService selectionService)
        : this(selectionService, new InMemorySceneSnapshotProvider(SceneSnapshot.Empty))
    {
    }

    internal InspectorPanelViewModel(
        IEditorSelectionService selectionService,
        ISceneSnapshotProvider sceneSnapshotProvider,
        IEditorUiDispatcher? uiDispatcher = null)
    {
        ArgumentNullException.ThrowIfNull(selectionService);
        ArgumentNullException.ThrowIfNull(sceneSnapshotProvider);

        selectionService_ = selectionService;
        sceneSnapshotProvider_ = sceneSnapshotProvider;
        uiDispatcher_ = uiDispatcher ?? new ImmediateEditorUiDispatcher();
        currentSelection_ = selectionService.Current;
        document_ = CreateDocument(currentSelection_, sceneSnapshotProvider_);
        selectionService_.SelectionChanged += OnSelectionChanged;
        sceneSnapshotProvider_.SnapshotChanged += OnSnapshotChanged;
    }

    public EditorSelectionSnapshot CurrentSelection
    {
        get => currentSelection_;
        private set => SetProperty(ref currentSelection_, value);
    }

    public InspectorDocumentModel? Document
    {
        get => document_;
        private set
        {
            if (SetProperty(ref document_, value))
            {
                OnPropertyChanged(nameof(HasDocument));
            }
        }
    }

    public bool HasDocument => Document is not null;

    public void Dispose()
    {
        selectionService_.SelectionChanged -= OnSelectionChanged;
        sceneSnapshotProvider_.SnapshotChanged -= OnSnapshotChanged;
    }

    private void OnSelectionChanged(object? sender, EditorSelectionChangedEventArgs e)
    {
        CurrentSelection = e.Current;
        Document = CreateDocument(e.Current, sceneSnapshotProvider_);
    }

    private void OnSnapshotChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshDocument();
            return;
        }

        uiDispatcher_.Post(RefreshDocument);
    }

    private void RefreshDocument()
    {
        Document = CreateDocument(CurrentSelection, sceneSnapshotProvider_);
    }

    private static InspectorDocumentModel? CreateDocument(
        EditorSelectionSnapshot selection,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        return selection.Items.Count switch
        {
            0 => null,
            1 => CreateSingleSelectionDocument(selection, sceneSnapshotProvider),
            _ => CreateMultiSelectionDocument(selection),
        };
    }

    private static InspectorDocumentModel CreateSingleSelectionDocument(
        EditorSelectionSnapshot selection,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        var item = selection.PrimaryItem!;
        if (!sceneSnapshotProvider.TryGetObject(item.Id, out var sceneObject) || sceneObject is null)
        {
            return CreateMissingSelectionDocument(selection, item);
        }

        var selectionProperties = new List<InspectorPropertyModel>
        {
            new("Name", sceneObject.DisplayName),
            new("Kind", sceneObject.Kind),
            new("Id", sceneObject.Id),
            new("Active", sceneObject.IsActive ? "True" : "False"),
        };

        if (!string.IsNullOrWhiteSpace(sceneObject.ParentId))
        {
            selectionProperties.Add(new InspectorPropertyModel("Parent", sceneObject.ParentId));
        }

        if (!string.IsNullOrWhiteSpace(sceneObject.IconKey))
        {
            selectionProperties.Add(new InspectorPropertyModel("Icon", sceneObject.IconKey));
        }

        if (!string.IsNullOrWhiteSpace(selection.ActiveContextId))
        {
            selectionProperties.Add(new InspectorPropertyModel("Context", selection.ActiveContextId));
        }

        var sections = new List<InspectorSectionModel>
        {
            new("Selection", selectionProperties),
        };

        if (sceneObject.Properties.Count > 0)
        {
            sections.Add(new InspectorSectionModel(
                "Properties",
                sceneObject.Properties
                    .Select(property => new InspectorPropertyModel(
                        property.DisplayName,
                        property.Value,
                        MapValueKind(property.ValueKind)))
                    .ToArray()));
        }

        return new InspectorDocumentModel(
            sceneObject.DisplayName,
            sceneObject.Kind,
            1,
            sections);
    }

    private static InspectorDocumentModel CreateMissingSelectionDocument(
        EditorSelectionSnapshot selection,
        EditorSelectionItem item)
    {
        var properties = new List<InspectorPropertyModel>
        {
            new("State", "Missing"),
            new("Id", item.Id),
        };

        if (!string.IsNullOrWhiteSpace(selection.ActiveContextId))
        {
            properties.Add(new InspectorPropertyModel("Context", selection.ActiveContextId));
        }

        return new InspectorDocumentModel(
            item.DisplayName,
            "Missing selection",
            1,
            [new InspectorSectionModel("Validation", properties)]);
    }

    private static InspectorDocumentModel CreateMultiSelectionDocument(EditorSelectionSnapshot selection)
    {
        var count = selection.Items.Count;
        var properties = new List<InspectorPropertyModel>
        {
            new("Count", count.ToString(CultureInfo.InvariantCulture), InspectorPropertyValueKind.Count),
        };

        if (!string.IsNullOrWhiteSpace(selection.ActiveContextId))
        {
            properties.Add(new InspectorPropertyModel("Context", selection.ActiveContextId));
        }

        return new InspectorDocumentModel(
            $"{count} items selected",
            "Multi-selection",
            count,
            [new InspectorSectionModel("Selection", properties)]);
    }

    private static InspectorPropertyValueKind MapValueKind(SceneObjectPropertyValueKind valueKind)
    {
        return valueKind switch
        {
            SceneObjectPropertyValueKind.Count => InspectorPropertyValueKind.Count,
            _ => InspectorPropertyValueKind.Text,
        };
    }
}
