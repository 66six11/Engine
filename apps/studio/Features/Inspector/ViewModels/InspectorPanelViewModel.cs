using System;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Shell.ViewModels;

namespace Editor.Features.Inspector.ViewModels;

public sealed class InspectorPanelViewModel : ViewModelBase, IDisposable
{
    private readonly IEditorSelectionService selectionService_;
    private EditorSelectionSnapshot currentSelection_;

    public InspectorPanelViewModel(IEditorSelectionService selectionService)
    {
        selectionService_ = selectionService;
        currentSelection_ = selectionService.Current;
        selectionService_.SelectionChanged += OnSelectionChanged;
    }

    public EditorSelectionSnapshot CurrentSelection
    {
        get => currentSelection_;
        private set => SetProperty(ref currentSelection_, value);
    }

    public void Dispose()
    {
        selectionService_.SelectionChanged -= OnSelectionChanged;
    }

    private void OnSelectionChanged(object? sender, EditorSelectionChangedEventArgs e)
    {
        CurrentSelection = e.Current;
    }
}
