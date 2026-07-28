using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Threading;
using CommunityToolkit.Mvvm.Input;
using Editor.Core.Abstractions;
using Asharia.Editor.Worlds.Snapshots;
using Asharia.Editor.Selection;
using Editor.Core.Services;
using Editor.Features.Hierarchy.Models;
using Editor.UI.Controls.Tree;
using Editor.UI.ViewModels;

namespace Editor.Features.Hierarchy.ViewModels;

public sealed class HierarchyPanelViewModel : ViewModelBase, IDisposable
{
    private const string SelectionContextId = "hierarchy";
    private readonly IEditorSelectionService selectionService_;
    private readonly ISceneSnapshotProvider sceneSnapshotProvider_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private EditorTreeExpansionState expansionState_ = new();
    private SceneSnapshot sceneSnapshot_ = SceneSnapshot.Empty;
    private IReadOnlyList<HierarchyNodeModel> nodes_ = [];
    private IReadOnlyList<EditorTreeNode<HierarchyNodeModel>> treeNodes_ = [];
    private IReadOnlyList<HierarchyNodeRowViewModel> visibleRows_ = [];
    private HierarchyNodeModel? selectedNode_;
    private HierarchyNodeRowViewModel? selectedRow_;
    private string filterText_ = string.Empty;
    private bool hasNoMatches_;
    private string nodeCountText_ = string.Empty;

    public HierarchyPanelViewModel(IEditorSelectionService selectionService)
        : this(selectionService, new InMemorySceneSnapshotProvider(SceneSnapshot.Empty))
    {
    }

    internal HierarchyPanelViewModel(
        IEditorSelectionService selectionService,
        ISceneSnapshotProvider sceneSnapshotProvider,
        IEditorUiDispatcher? uiDispatcher = null)
    {
        ArgumentNullException.ThrowIfNull(selectionService);
        ArgumentNullException.ThrowIfNull(sceneSnapshotProvider);

        selectionService_ = selectionService;
        sceneSnapshotProvider_ = sceneSnapshotProvider;
        uiDispatcher_ = uiDispatcher ?? new ImmediateEditorUiDispatcher();

        ToggleExpandedCommand = new RelayCommand<HierarchyNodeRowViewModel>(ToggleExpanded);
        LoadSnapshot(sceneSnapshotProvider_.GetCurrentSnapshot(), preserveExpandedState: false);
        sceneSnapshotProvider_.SnapshotChanged += OnSnapshotChanged;
    }

    public SceneSnapshot SceneSnapshot
    {
        get => sceneSnapshot_;
        private set => SetProperty(ref sceneSnapshot_, value);
    }

    public IReadOnlyList<HierarchyNodeModel> Nodes
    {
        get => nodes_;
        private set => SetProperty(ref nodes_, value);
    }

    public IReadOnlyList<HierarchyNodeRowViewModel> VisibleRows
    {
        get => visibleRows_;
        private set => SetProperty(ref visibleRows_, value);
    }

    public HierarchyNodeRowViewModel? SelectedRow
    {
        get => selectedRow_;
        set
        {
            if (!SetProperty(ref selectedRow_, value))
            {
                return;
            }

            if (value is not null)
            {
                SelectedNode = value.Node;
            }
        }
    }

    public HierarchyNodeModel? SelectedNode
    {
        get => selectedNode_;
        set
        {
            if (!SetProperty(ref selectedNode_, value))
            {
                return;
            }

            if (value is null)
            {
                ClearSelection();
                SyncSelectedRow();
                return;
            }

            SelectItem(value.ToSelectionItem());
            SyncSelectedRow();
        }
    }

    public string? FilterText
    {
        get => filterText_;
        set
        {
            if (SetProperty(ref filterText_, value ?? string.Empty))
            {
                RefreshVisibleRows();
                OnPropertyChanged(nameof(HasFilter));
            }
        }
    }

    public bool HasFilter => !string.IsNullOrWhiteSpace(filterText_);

    public bool HasNoMatches
    {
        get => hasNoMatches_;
        private set => SetProperty(ref hasNoMatches_, value);
    }

    public string NodeCountText
    {
        get => nodeCountText_;
        private set => SetProperty(ref nodeCountText_, value);
    }

    public IRelayCommand<HierarchyNodeRowViewModel> ToggleExpandedCommand { get; }

    public void Dispose()
    {
        sceneSnapshotProvider_.SnapshotChanged -= OnSnapshotChanged;
    }

    public void SelectItem(EditorSelectionItem item)
    {
        selectionService_.ReplaceSelection(SelectionContextId, [item]);
    }

    public void ClearSelection()
    {
        selectionService_.ClearSelection(SelectionContextId);
    }

    private void OnSnapshotChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshSnapshot();
            return;
        }

        uiDispatcher_.Post(RefreshSnapshot);
    }

    private void RefreshSnapshot()
    {
        LoadSnapshot(sceneSnapshotProvider_.GetCurrentSnapshot(), preserveExpandedState: true);
    }

    private void LoadSnapshot(SceneSnapshot snapshot, bool preserveExpandedState)
    {
        var previousExpandedNodeIds = preserveExpandedState
            ? expansionState_.ToHashSet()
            : [];

        SceneSnapshot = snapshot;
        Nodes = SceneSnapshot.Objects
            .Select(HierarchyNodeModel.FromSceneObject)
            .ToArray();
        treeNodes_ = Nodes
            .Select(node => new EditorTreeNode<HierarchyNodeModel>(node.Id, node.ParentId, node))
            .ToArray();
        var nodeIdsWithChildren = treeNodes_
            .Where(node => !string.IsNullOrWhiteSpace(node.ParentId))
            .Select(node => node.ParentId!)
            .ToHashSet(StringComparer.Ordinal);
        expansionState_ = new EditorTreeExpansionState(treeNodes_
            .Where(node => nodeIdsWithChildren.Contains(node.Id)
                && (preserveExpandedState
                    ? previousExpandedNodeIds.Contains(node.Id)
                    : node.ParentId is null))
            .Select(node => node.Id));

        RefreshVisibleRows();
    }

    private void ToggleExpanded(HierarchyNodeRowViewModel? row)
    {
        if (row is null || !expansionState_.Toggle(row.Id, row.HasChildren))
        {
            return;
        }

        RefreshVisibleRows();
    }

    private void RefreshVisibleRows()
    {
        var query = filterText_.Trim();
        var hasFilter = query.Length > 0;
        Func<EditorTreeNode<HierarchyNodeModel>, bool>? searchPredicate = hasFilter
            ? node => MatchesQuery(node.Payload, query)
            : null;
        var treeRows = EditorTreeFlattener.Flatten(
            treeNodes_,
            expansionState_,
            searchPredicate);

        VisibleRows = treeRows
            .Select(CreateRow)
            .ToArray();

        HasNoMatches = hasFilter && VisibleRows.Count == 0;
        NodeCountText = hasFilter
            ? $"{VisibleRows.Count}/{Nodes.Count}"
            : $"{Nodes.Count}";
        SyncSelectedRow();
    }

    private HierarchyNodeRowViewModel CreateRow(
        EditorTreeRow<HierarchyNodeModel> row)
    {
        return new HierarchyNodeRowViewModel(
            row.Payload,
            row.Depth,
            row.HasChildren,
            row.IsExpanded,
            row.IsLastSibling,
            row.AncestorContinuationMask,
            row.IsSearchMatch,
            ToggleExpandedCommand);
    }

    private void SyncSelectedRow()
    {
        var row = selectedNode_ is null
            ? null
            : VisibleRows.FirstOrDefault(candidate => candidate.Id == selectedNode_.Id);
        SetProperty(ref selectedRow_, row, nameof(SelectedRow));
    }

    private static bool MatchesQuery(HierarchyNodeModel node, string query)
    {
        return node.DisplayName.Contains(query, StringComparison.OrdinalIgnoreCase)
            || node.Kind.Contains(query, StringComparison.OrdinalIgnoreCase)
            || node.Id.Contains(query, StringComparison.OrdinalIgnoreCase);
    }
}
