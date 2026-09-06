using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Actions;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Avalonia.Threading;
using Editor.Shell.Actions;

namespace Editor.Shell.ViewModels.Windowing;

internal sealed record StudioMeshChoice(Guid? AssetId, string Label)
{
    public override string ToString() => Label;
}

internal sealed partial class StudioShellViewModel
{
    private IReadOnlyList<StudioMeshChoice> meshChoices_ = [new(null, "None")];
    private StudioMeshChoice? selectedMeshChoice_;
    private bool refreshingMeshChoices_;

    public IReadOnlyList<StudioMeshChoice> MeshChoices => meshChoices_;

    public StudioMeshChoice? SelectedMeshChoice
    {
        get => selectedMeshChoice_;
        set
        {
            if (refreshingMeshChoices_ || Equals(selectedMeshChoice_, value)) return;
            selectedMeshChoice_ = value;
            OnPropertyChanged();
            RaiseProjectCommandStateChanged();
        }
    }

    private IEnumerable<AssetCatalogEntry> CurrentMeshEntries()
    {
        var snapshot = projectAssetCatalog_.Current;
        var project = projectSnapshot_.Project;
        if (project is null || snapshot.Scope?.SessionId != project.SessionId ||
            snapshot.Scope.ProjectId != project.ProjectId ||
            snapshot.State is not (AssetCatalogSessionState.Ready or AssetCatalogSessionState.Degraded) ||
            snapshot.Catalog?.ProjectId != project.ProjectId)
        {
            return [];
        }
        // Duplicate identities cannot be safely assigned, including collisions with non-Mesh assets.
        return snapshot.Catalog.Entries.Where(entry => entry.AssetGuid is not null)
            .GroupBy(entry => entry.AssetGuid)
            .Where(group => group.Count() == 1)
            .Select(group => group.Single())
            .Where(entry => entry.AssetGuid != Guid.Empty &&
                entry.AssetTypeName == "com.asharia.asset.Mesh");
    }

    private void RefreshMeshChoices(SceneEntitySnapshot? entity)
    {
        var choices = new List<StudioMeshChoice> { new(null, "None") };
        choices.AddRange(CurrentMeshEntries().OrderBy(entry => entry.SourcePath, StringComparer.Ordinal)
            .Select(entry => new StudioMeshChoice(entry.AssetGuid, entry.SourcePath)));
        var currentId = entity?.Mesh?.AssetId;
        if (currentId is not null && choices.All(choice => choice.AssetId != currentId))
        {
            choices.Add(new(currentId, $"Unavailable: {currentId:D}"));
        }
        refreshingMeshChoices_ = true;
        try
        {
            meshChoices_ = choices;
            selectedMeshChoice_ = choices.First(choice => choice.AssetId == currentId);
            OnPropertyChanged(nameof(MeshChoices));
            OnPropertyChanged(nameof(SelectedMeshChoice));
        }
        finally
        {
            refreshingMeshChoices_ = false;
        }
        RaiseProjectCommandStateChanged();
    }

    private void OnMeshCatalogChanged(object? sender, AssetCatalogSessionSnapshotChangedEventArgs args)
    {
        void Refresh()
        {
            if (!isDisposed_) RefreshMeshChoices(selectedEntity_);
        }
        if (Dispatcher.UIThread.CheckAccess()) Refresh();
        else Dispatcher.UIThread.Post(Refresh);
    }

    private StudioActionState EvaluateMeshAction(StudioActionContextSnapshot context)
    {
        var state = EvaluateSelectionAction(context);
        if (!state.IsEnabled) return state;
        var choice = selectedMeshChoice_;
        if (choice is null || (choice.AssetId is Guid id &&
            !CurrentMeshEntries().Any(entry => entry.AssetGuid == id)))
        {
            return StudioActionState.Blocked(StudioActionBlockKind.Disabled,
                "Choose an available Mesh asset or None.");
        }
        return state;
    }

    private ValueTask<StudioActionCompletion> HandleApplyEntityMeshAsync(
        StudioActionContextSnapshot context, CancellationToken cancellationToken)
    {
        var mesh = selectedMeshChoice_!.AssetId is Guid id ? new SceneMeshReference(id) : (SceneMeshReference?)null;
        var revision = projectSnapshot_.Document!.Revision;
        var editId = ProjectEditId.CreateNew();
        return ExecuteProjectActionAsync(StudioShellActionIds.ApplyEntityMesh, context, cancellationToken,
            token => projectSession_.SetEntityMeshAsync(context.Target.ObjectId!.Value, mesh,
                new ProjectSessionEditContext(editId, revision), token));
    }
}
