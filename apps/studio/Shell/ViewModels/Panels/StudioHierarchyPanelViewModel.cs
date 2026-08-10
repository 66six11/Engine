using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Editor.Shell.ViewModels.Windowing;
using Editor.UI.Icons;

namespace Editor.Shell.ViewModels.Panels;

internal sealed class StudioHierarchyPanelViewModel :
    StudioDockPanelViewModel,
    INotifyPropertyChanged,
    IDisposable
{
    private static readonly IReadOnlyList<StudioHierarchyRowViewModel> EmptyRows =
        Array.Empty<StudioHierarchyRowViewModel>();

    private string filterText_ = string.Empty;
    private IReadOnlyList<StudioHierarchyRowViewModel> visibleRows_ = EmptyRows;
    private StudioHierarchyRowViewModel? selectedRow_;
    private ProjectSessionId documentSessionId_;
    private Guid documentSceneId_;
    private bool isSceneExpanded_ = true;
    private string entityCountText_ = "0";
    private bool isEmptyStateVisible_ = true;
    private string emptyStateText_ = "No scene loaded";
    private bool isReplacingProjection_;
    private bool isDisposed_;

    public StudioHierarchyPanelViewModel(StudioShellViewModel shell)
        : base(shell)
    {
        Shell.PropertyChanged += OnShellPropertyChanged;
        RebuildProjection();
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public string FilterText
    {
        get => filterText_;
        set
        {
            var normalized = value ?? string.Empty;
            if (string.Equals(filterText_, normalized, StringComparison.Ordinal))
            {
                return;
            }

            filterText_ = normalized;
            OnPropertyChanged();
            RebuildProjection();
        }
    }

    public IReadOnlyList<StudioHierarchyRowViewModel> VisibleRows => visibleRows_;

    public StudioHierarchyRowViewModel? SelectedRow
    {
        get => selectedRow_;
        set
        {
            if (ReferenceEquals(selectedRow_, value))
            {
                return;
            }

            if (value is null && Shell.SelectedEntity is { } selectedEntity
                && (isReplacingProjection_
                    || !visibleRows_.Any(
                        row => row.Entity?.ObjectId == selectedEntity.ObjectId)))
            {
                OnPropertyChanged();
                return;
            }

            selectedRow_ = value;
            Shell.SelectedEntity = value?.Entity;
            OnPropertyChanged();
        }
    }

    public string EntityCountText => entityCountText_;

    public bool IsEmptyStateVisible => isEmptyStateVisible_;

    public string EmptyStateText => emptyStateText_;

    public bool IsSceneExpanded => isSceneExpanded_;

    public void ToggleExpanded(StudioHierarchyRowViewModel row)
    {
        ArgumentNullException.ThrowIfNull(row);
        if (!row.IsSceneRoot || !row.HasChildren)
        {
            return;
        }

        isSceneExpanded_ = !isSceneExpanded_;
        OnPropertyChanged(nameof(IsSceneExpanded));
        RebuildProjection();
    }

    public void Dispose()
    {
        if (isDisposed_)
        {
            return;
        }

        isDisposed_ = true;
        Shell.PropertyChanged -= OnShellPropertyChanged;
    }

    private void OnShellPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (isDisposed_)
        {
            return;
        }

        if (string.Equals(e.PropertyName, nameof(StudioShellViewModel.SelectedEntity),
                StringComparison.Ordinal))
        {
            SynchronizeVisibleSelection();
            return;
        }

        if (string.Equals(e.PropertyName, nameof(StudioShellViewModel.SceneEntities),
                StringComparison.Ordinal)
            || string.Equals(e.PropertyName, nameof(StudioShellViewModel.HasDocument),
                StringComparison.Ordinal)
            || string.Equals(e.PropertyName, nameof(StudioShellViewModel.DocumentPathText),
                StringComparison.Ordinal))
        {
            RebuildProjection();
        }
    }

    private void RebuildProjection()
    {
        var snapshot = Shell.AppliedProjectSnapshot;
        var document = snapshot.Document;
        if (document is null || snapshot.Project is null)
        {
            ResetDocumentScope();
            ReplaceProjection(EmptyRows, "0", "No scene loaded");
            return;
        }

        UpdateDocumentScope(snapshot.Project.SessionId, document.SceneId);

        var query = filterText_.Trim();
        var hasFilter = query.Length != 0;
        var rootName = GetSceneDisplayName(document.Path);
        var rootMatches = hasFilter && Matches(
            query,
            rootName,
            "Scene",
            document.SceneId);

        var matchingEntities = hasFilter && !rootMatches
            ? document.Entities.Where(entity => Matches(
                query,
                entity.Name,
                "Entity",
                entity.ObjectId)).ToArray()
            : document.Entities.ToArray();

        if (hasFilter && !rootMatches && matchingEntities.Length == 0)
        {
            ReplaceProjection(
                EmptyRows,
                $"0/{document.Entities.Count}",
                "No matching objects");
            return;
        }

        var showChildren = hasFilter || isSceneExpanded_;
        var rows = new List<StudioHierarchyRowViewModel>(
            1 + (showChildren ? matchingEntities.Length : 0));
        rows.Add(StudioHierarchyRowViewModel.CreateSceneRoot(
            document.SceneId,
            rootName,
            document.Entities.Count != 0,
            isSceneExpanded_ || hasFilter));

        if (showChildren)
        {
            for (var index = 0; index < matchingEntities.Length; index++)
            {
                rows.Add(StudioHierarchyRowViewModel.CreateEntity(
                    matchingEntities[index],
                    isLastSibling: index == matchingEntities.Length - 1));
            }
        }

        var countText = hasFilter
            ? $"{matchingEntities.Length}/{document.Entities.Count}"
            : document.Entities.Count.ToString(System.Globalization.CultureInfo.InvariantCulture);
        ReplaceProjection(rows.ToArray(), countText, emptyStateText: string.Empty);
    }

    private void ReplaceProjection(
        IReadOnlyList<StudioHierarchyRowViewModel> rows,
        string countText,
        string emptyStateText)
    {
        visibleRows_ = rows;
        entityCountText_ = countText;
        emptyStateText_ = emptyStateText;
        isEmptyStateVisible_ = rows.Count == 0;
        var nextSelection = ResolveVisibleSelection();
        var selectionChanged = !ReferenceEquals(selectedRow_, nextSelection);
        selectedRow_ = nextSelection;

        isReplacingProjection_ = true;
        try
        {
            OnPropertyChanged(nameof(VisibleRows));
            OnPropertyChanged(nameof(EntityCountText));
            OnPropertyChanged(nameof(EmptyStateText));
            OnPropertyChanged(nameof(IsEmptyStateVisible));
            if (selectionChanged)
            {
                OnPropertyChanged(nameof(SelectedRow));
            }
        }
        finally
        {
            isReplacingProjection_ = false;
        }
    }

    private void SynchronizeVisibleSelection()
    {
        var nextSelection = ResolveVisibleSelection();

        if (ReferenceEquals(selectedRow_, nextSelection))
        {
            return;
        }

        selectedRow_ = nextSelection;
        OnPropertyChanged(nameof(SelectedRow));
    }

    private StudioHierarchyRowViewModel? ResolveVisibleSelection()
    {
        var selectedObjectId = Shell.SelectedEntity?.ObjectId;
        return selectedObjectId is { } objectId
            ? visibleRows_.FirstOrDefault(row => row.Entity?.ObjectId == objectId)
            : selectedRow_ is { IsSceneRoot: true }
                ? visibleRows_.FirstOrDefault(row => row.IsSceneRoot)
                : null;
    }

    private void UpdateDocumentScope(ProjectSessionId sessionId, Guid sceneId)
    {
        if (documentSessionId_ == sessionId && documentSceneId_ == sceneId)
        {
            return;
        }

        ClearSceneRootSelection();
        documentSessionId_ = sessionId;
        documentSceneId_ = sceneId;
        if (!isSceneExpanded_)
        {
            isSceneExpanded_ = true;
            OnPropertyChanged(nameof(IsSceneExpanded));
        }
    }

    private void ResetDocumentScope()
    {
        ClearSceneRootSelection();
        documentSessionId_ = default;
        documentSceneId_ = Guid.Empty;
        if (!isSceneExpanded_)
        {
            isSceneExpanded_ = true;
            OnPropertyChanged(nameof(IsSceneExpanded));
        }
    }

    private void ClearSceneRootSelection()
    {
        if (selectedRow_ is not { IsSceneRoot: true })
        {
            return;
        }

        selectedRow_ = null;
        OnPropertyChanged(nameof(SelectedRow));
    }

    private static string GetSceneDisplayName(string path)
    {
        const string sceneDocumentSuffix = ".asharia.scene.json";
        var fullFileName = Path.GetFileName(path);
        var fileName = fullFileName.EndsWith(
            sceneDocumentSuffix,
            StringComparison.OrdinalIgnoreCase)
            ? fullFileName[..^sceneDocumentSuffix.Length]
            : Path.GetFileNameWithoutExtension(fullFileName);
        return string.IsNullOrWhiteSpace(fileName) ? "Scene" : fileName;
    }

    private static bool Matches(string query, string name, string typeName, Guid id) =>
        name.Contains(query, StringComparison.OrdinalIgnoreCase)
        || typeName.Contains(query, StringComparison.OrdinalIgnoreCase)
        || id.ToString("D").Contains(query, StringComparison.OrdinalIgnoreCase)
        || id.ToString("N").Contains(query, StringComparison.OrdinalIgnoreCase);

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}

internal sealed class StudioHierarchyRowViewModel
{
    private StudioHierarchyRowViewModel(
        Guid stableId,
        SceneEntitySnapshot? entity,
        string displayName,
        string typeName,
        string iconKey,
        bool isSceneRoot,
        bool hasChildren,
        bool isExpanded,
        bool showIndentGuide,
        bool isLastSibling)
    {
        StableId = stableId;
        Entity = entity;
        DisplayName = displayName;
        TypeName = typeName;
        IconKey = iconKey;
        IsSceneRoot = isSceneRoot;
        HasChildren = hasChildren;
        IsExpanded = isExpanded;
        ShowIndentGuide = showIndentGuide;
        IsLastSibling = isLastSibling;
    }

    public Guid StableId { get; }

    public SceneEntitySnapshot? Entity { get; }

    public string DisplayName { get; }

    public string TypeName { get; }

    public string IconKey { get; }

    public bool IsSceneRoot { get; }

    public bool HasChildren { get; }

    public bool IsExpanded { get; }

    public bool ShowIndentGuide { get; }

    public bool IsLastSibling { get; }

    public double IndentWidth => ShowIndentGuide ? 12d : 0d;

    public double GuideHeight => IsLastSibling ? 10d : 20d;

    public string ExpanderIconKey => IsExpanded
        ? EditorIconKey.UiChevronDown
        : EditorIconKey.UiChevronRight;

    public string AutomationName => $"{DisplayName}, {TypeName}";

    public static StudioHierarchyRowViewModel CreateSceneRoot(
        Guid sceneId,
        string displayName,
        bool hasChildren,
        bool isExpanded) =>
        new(
            sceneId,
            entity: null,
            displayName,
            "Scene",
            EditorIconKey.PanelHierarchy,
            isSceneRoot: true,
            hasChildren,
            isExpanded,
            showIndentGuide: false,
            isLastSibling: true);

    public static StudioHierarchyRowViewModel CreateEntity(
        SceneEntitySnapshot entity,
        bool isLastSibling)
    {
        ArgumentNullException.ThrowIfNull(entity);
        return new StudioHierarchyRowViewModel(
            entity.ObjectId,
            entity,
            entity.Name,
            "Entity",
            EditorIconKey.ObjectDefault,
            isSceneRoot: false,
            hasChildren: false,
            isExpanded: false,
            showIndentGuide: true,
            isLastSibling);
    }
}
