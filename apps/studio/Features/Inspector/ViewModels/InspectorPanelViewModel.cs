using System;
using System.Collections.Generic;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Features.Inspector.Models;
using Editor.Shell.ViewModels;

namespace Editor.Features.Inspector.ViewModels;

public sealed class InspectorPanelViewModel : ViewModelBase, IDisposable
{
    private readonly IEditorSelectionService selectionService_;
    private EditorSelectionSnapshot currentSelection_;
    private InspectorDocumentModel? document_;

    public InspectorPanelViewModel(IEditorSelectionService selectionService)
    {
        selectionService_ = selectionService;
        currentSelection_ = selectionService.Current;
        document_ = CreateDocument(currentSelection_);
        selectionService_.SelectionChanged += OnSelectionChanged;
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
    }

    private void OnSelectionChanged(object? sender, EditorSelectionChangedEventArgs e)
    {
        CurrentSelection = e.Current;
        Document = CreateDocument(e.Current);
    }

    private static InspectorDocumentModel? CreateDocument(EditorSelectionSnapshot selection)
    {
        return selection.Items.Count switch
        {
            0 => null,
            1 => CreateSingleSelectionDocument(selection),
            _ => CreateMultiSelectionDocument(selection),
        };
    }

    private static InspectorDocumentModel CreateSingleSelectionDocument(EditorSelectionSnapshot selection)
    {
        var item = selection.PrimaryItem!;
        var properties = new List<InspectorPropertyModel>
        {
            new("Name", item.DisplayName),
            new("Kind", item.Kind),
            new("Id", item.Id),
        };

        if (!string.IsNullOrWhiteSpace(item.IconKey))
        {
            properties.Add(new InspectorPropertyModel("Icon", item.IconKey));
        }

        if (!string.IsNullOrWhiteSpace(selection.ActiveContextId))
        {
            properties.Add(new InspectorPropertyModel("Context", selection.ActiveContextId));
        }

        return new InspectorDocumentModel(
            item.DisplayName,
            item.Kind,
            1,
            [new InspectorSectionModel("Selection", properties)]);
    }

    private static InspectorDocumentModel CreateMultiSelectionDocument(EditorSelectionSnapshot selection)
    {
        var count = selection.Items.Count;
        var properties = new List<InspectorPropertyModel>
        {
            new("Count", count.ToString(System.Globalization.CultureInfo.InvariantCulture), InspectorPropertyValueKind.Count),
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
}
