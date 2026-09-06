using System;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.TestSupport;
using Avalonia.Controls;
using Avalonia.Automation;
using Avalonia.Headless.XUnit;
using Avalonia.LogicalTree;
using Avalonia.Threading;
using Editor.Shell.ViewModels.Panels;
using Editor.Shell.ViewModels.Windowing;
using Editor.Shell.Views.Panels;
using Editor.Shell.Views.Windowing;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioInspectorPanelHeadlessTests
{
    [AvaloniaFact]
    public async Task Mesh_picker_filters_assets_and_routes_apply_and_remove_through_document_command()
    {
        var session = new TestProjectSession();
        var id = Guid.NewGuid();
        var entity = new SceneEntitySnapshot(id, new EntityId(1, 1), "Entity", TransformValue.Identity);
        var initial = Ready(Guid.NewGuid(), 1, entity);
        var meshId = Guid.NewGuid();
        var duplicateId = Guid.NewGuid();
        var replacementId = Guid.NewGuid();
        var snapshot = AssetSnapshot(initial.Project!.ProjectId,
            AssetEntry(meshId, assetType: "com.asharia.asset.Mesh"),
            AssetEntry(replacementId, assetType: "com.asharia.asset.Mesh"),
            AssetEntry(Guid.NewGuid()),
            AssetEntry(duplicateId, assetType: "com.asharia.asset.Mesh"),
            AssetEntry(duplicateId));
        await using var catalog = new ProjectAssetCatalog(session,
            new SingleAssetCatalogGateway(AssetCatalogQueryResult.Success(snapshot)));
        using var shell = new StudioShellViewModel(session, new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(session),
            StudioShellTestFactory.CreateDiagnosticWriter(), catalog,
            StudioShellTestFactory.CreateEditorSelectionService());
        session.Publish(initial);
        shell.MarkReady();
        shell.SelectedEntity = entity;
        await WaitUntilAsync(() => catalog.Current.State == AssetCatalogSessionState.Ready);
        Assert.Equal(3, shell.MeshChoices.Count);
        Assert.Contains(shell.MeshChoices, choice => choice.AssetId == meshId);
        using var model = new StudioInspectorPanelViewModel(shell, catalog, shell.EditorSelection);
        var view = new StudioInspectorPanelView { DataContext = model, Width = 244 };
        var window = new MainWindow { Width = 260, Height = 480, Content = view };
        var calls = 0;
        session.SetMeshHandler = (objectId, mesh, context, _) =>
        {
            Assert.Equal(id, objectId);
            Assert.Equal(session.Current.Document!.Revision, context.ExpectedRevision);
            var updatedEntity = new SceneEntitySnapshot(id, entity.RuntimeEntityId, entity.Name,
                entity.Transform, mesh);
            var updated = ProjectSessionSnapshot.Ready(initial.Project!,
                new SceneDocumentSnapshot(initial.Document!.SceneId, initial.Document.Path,
                    context.ExpectedRevision + 1, 1, [updatedEntity]),
                new ContentStateId((ulong)++calls + 1), new ContentStateId(1),
                canUndo: true, canRedo: false, undoLabel: "Edit Mesh", redoLabel: null);
            session.Publish(updated, context.EditId, originatingEditSucceeded: true);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(updated, "Mesh updated",
                originatingEditId: context.EditId));
        };
        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var picker = Assert.IsType<ComboBox>(view.FindControl<ComboBox>("InspectorMeshChoice"));
            var apply = Assert.IsType<Button>(view.FindControl<Button>("ApplyMeshButton"));
            picker.SelectedItem = shell.MeshChoices.Single(choice => choice.AssetId == meshId);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(meshId, shell.SelectedMeshChoice!.AssetId);
            Assert.True(apply.Command!.CanExecute(apply.CommandParameter));
            apply.Command.Execute(apply.CommandParameter);
            await WaitUntilAsync(() => calls == 1 && !shell.IsProjectOperationRunning);
            Assert.Equal(meshId, session.Current.Document!.Entities.Single().Mesh!.Value.AssetId);
            picker.SelectedItem = shell.MeshChoices.Single(choice => choice.AssetId == replacementId);
            Dispatcher.UIThread.RunJobs();
            apply.Command.Execute(apply.CommandParameter);
            await WaitUntilAsync(() => calls == 2 && !shell.IsProjectOperationRunning);
            Assert.Equal(replacementId, session.Current.Document!.Entities.Single().Mesh!.Value.AssetId);
            picker.SelectedItem = shell.MeshChoices.Single(choice => choice.AssetId is null);
            Dispatcher.UIThread.RunJobs();
            apply.Command.Execute(apply.CommandParameter);
            await WaitUntilAsync(() => calls == 3 && !shell.IsProjectOperationRunning);
            Assert.Null(session.Current.Document!.Entities.Single().Mesh);
            Assert.True(picker.Bounds.Width <= 244);
            Assert.Equal("Mesh asset", picker.GetValue(AutomationProperties.NameProperty));
        }
        finally { window.Close(); }
    }

    [AvaloniaFact]
    public void Missing_mesh_reference_remains_visible_and_selection_change_discards_draft()
    {
        using var shell = StudioShellTestFactory.Create(out var session, out _);
        var mesh = new SceneMeshReference(Guid.NewGuid());
        var first = new SceneEntitySnapshot(Guid.NewGuid(), new EntityId(1, 1), "First", TransformValue.Identity, mesh);
        var second = new SceneEntitySnapshot(Guid.NewGuid(), new EntityId(2, 1), "Second", TransformValue.Identity);
        session.Publish(Ready(Guid.NewGuid(), 1, first, second));
        shell.MarkReady();
        shell.SelectedEntity = first;
        Assert.Equal(mesh.AssetId, shell.SelectedMeshChoice!.AssetId);
        Assert.Contains(mesh.AssetId.ToString("D"), shell.SelectedMeshChoice.Label);
        var apply = shell.GetActionCommand(Editor.Shell.Actions.StudioShellActionIds.ApplyEntityMesh);
        Assert.False(apply.CanExecute(null));
        shell.SelectedMeshChoice = shell.MeshChoices.Single(choice => choice.AssetId is null);
        Assert.True(apply.CanExecute(null));
        shell.SelectedEntity = second;
        Assert.Null(shell.SelectedMeshChoice!.AssetId);
        shell.SelectedEntity = first;
        Assert.Equal(mesh.AssetId, shell.SelectedMeshChoice!.AssetId);
    }

    [AvaloniaFact]
    public async Task Asset_selection_shows_structured_read_only_catalog_facts()
    {
        using var shell = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var initial = Ready(Guid.NewGuid(), revision: 1);
        var assetGuid = Guid.NewGuid();
        var entry = AssetEntry(assetGuid);
        var gateway = new SingleAssetCatalogGateway(
            AssetCatalogQueryResult.Success(AssetSnapshot(
                initial.Project!.ProjectId,
                entry)));
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        using var viewModel = new StudioInspectorPanelViewModel(
            shell,
            catalog,
            shell.EditorSelection);
        var view = new StudioInspectorPanelView { DataContext = viewModel };
        var window = new Window { Width = 260, Height = 420, Content = view };

        try
        {
            projectSession.Publish(initial);
            window.Show();
            await WaitUntilAsync(() => catalog.Current.State == AssetCatalogSessionState.Ready);
            var scope = catalog.Current.Scope!;
            shell.EditorSelection.Replace(new AssetSelectionTarget(
                scope.SessionId,
                scope.ProjectId,
                scope.TargetProfile,
                entry.SelectionKey));
            await WaitUntilAsync(() => viewModel.IsAssetSelection);

            Assert.True(Assert.IsType<ScrollViewer>(
                view.FindControl<ScrollViewer>("InspectorAssetContent")).IsVisible);
            Assert.False(Assert.IsType<ScrollViewer>(
                view.FindControl<ScrollViewer>("InspectorEntityContent")).IsVisible);
            Assert.False(Assert.IsType<TextBlock>(
                view.FindControl<TextBlock>("InspectorEmptyState")).IsVisible);
            Assert.Equal(1, Assert.IsType<ListBox>(
                view.FindControl<ListBox>("InspectorSubAssets")).ItemCount);
            Assert.Equal(1, Assert.IsType<ListBox>(
                view.FindControl<ListBox>("InspectorAssetDiagnostics")).ItemCount);
            Assert.Equal("Assets/Models/Wedge.glb", viewModel.Asset?.SourcePath);
            Assert.Equal("Ready", viewModel.Asset?.CatalogState);
            Assert.Equal("9", viewModel.Asset?.CatalogRevision);
            Assert.Equal("Current", viewModel.Asset?.ProductState);
            Assert.Empty(Assert.IsType<ScrollViewer>(
                view.FindControl<ScrollViewer>("InspectorAssetContent"))
                .GetLogicalDescendants()
                .OfType<TextBox>());
            Assert.Empty(Assert.IsType<ScrollViewer>(
                view.FindControl<ScrollViewer>("InspectorAssetContent"))
                .GetLogicalDescendants()
                .OfType<Button>());
            Assert.Equal("Inspector", AutomationProperties.GetName(view));
            Assert.Equal("Asset sub-assets", AutomationProperties.GetName(
                Assert.IsType<ListBox>(view.FindControl<ListBox>("InspectorSubAssets"))));
            Assert.True(view.DesiredSize.Width <= 260d);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async Task Stale_product_remains_a_distinct_read_only_asset_selection()
    {
        using var shell = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var initial = Ready(Guid.NewGuid(), revision: 1);
        var entry = AssetEntry(Guid.NewGuid(), AssetCatalogProductState.Stale);
        var gateway = new SingleAssetCatalogGateway(
            AssetCatalogQueryResult.Success(AssetSnapshot(
                initial.Project!.ProjectId,
                entry)));
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        using var viewModel = new StudioInspectorPanelViewModel(
            shell,
            catalog,
            shell.EditorSelection);
        var view = new StudioInspectorPanelView { DataContext = viewModel };
        var window = new Window { Width = 260, Height = 240, Content = view };

        try
        {
            projectSession.Publish(initial);
            window.Show();
            await WaitUntilAsync(() => catalog.Current.State == AssetCatalogSessionState.Ready);
            var scope = catalog.Current.Scope!;
            shell.EditorSelection.Replace(new AssetSelectionTarget(
                scope.SessionId,
                scope.ProjectId,
                scope.TargetProfile,
                entry.SelectionKey));
            await WaitUntilAsync(() => viewModel.IsAssetSelection);

            Assert.False(viewModel.IsEmptySelection);
            Assert.Equal("Stale", viewModel.Asset?.ProductState);
            Assert.True(Assert.IsType<ScrollViewer>(
                view.FindControl<ScrollViewer>("InspectorAssetContent")).IsVisible);
            Assert.False(Assert.IsType<TextBlock>(
                view.FindControl<TextBlock>("InspectorEmptyState")).IsVisible);
            Assert.Equal("Inspector", AutomationProperties.GetName(view));
            Assert.True(view.DesiredSize.Width <= 260d);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public async Task Euler_degree_text_input_publishes_a_local_rotation_request()
    {
        var objectId = Guid.NewGuid();
        var runtimeEntityId = new EntityId(1, 1);
        var sceneId = Guid.NewGuid();
        var entity = new SceneEntitySnapshot(
            objectId,
            runtimeEntityId,
            "Directional Wedge",
            TransformValue.Identity,
            SceneMeshReference.DirectionalWedgeValidation);
        var initial = Ready(sceneId, revision: 1, entity);
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        projectSession.Publish(initial);
        shell.MarkReady();
        shell.SelectedEntity = entity;
        TransformValue? requestedTransform = null;
        projectSession.SetTransformHandler = (requestedObjectId, transform, editContext, _) =>
        {
            Assert.Equal(objectId, requestedObjectId);
            requestedTransform = transform;
            var transformedEntity = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                entity.Name,
                transform,
                entity.Mesh);
            var updated = ProjectSessionSnapshot.Ready(
                initial.Project!,
                new SceneDocumentSnapshot(
                    sceneId,
                    initial.Document!.Path,
                    revision: 2,
                    savedRevision: 1,
                    entities: [transformedEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
            projectSession.Publish(
                updated,
                editContext.EditId,
                originatingEditSucceeded: true);
            return ValueTask.FromResult(
                ProjectSessionOperationResult.Success(
                    updated,
                    "Updated entity Transform.",
                    originatingEditId: editContext.EditId));
        };
        using var viewModel = new StudioInspectorPanelViewModel(
            shell,
            shell.ProjectAssetCatalog,
            shell.EditorSelection);
        var view = new StudioInspectorPanelView
        {
            DataContext = viewModel,
        };
        var window = new MainWindow
        {
            Width = 420,
            Height = 520,
            Content = view,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var rotationX = Assert.IsType<TextBox>(
                view.FindControl<TextBox>("InspectorRotationDegreesX"));
            var rotationY = Assert.IsType<TextBox>(
                view.FindControl<TextBox>("InspectorRotationDegreesY"));
            var rotationZ = Assert.IsType<TextBox>(
                view.FindControl<TextBox>("InspectorRotationDegreesZ"));
            var positionX = Assert.IsType<TextBox>(
                view.FindControl<TextBox>("InspectorPositionX"));
            var scaleX = Assert.IsType<TextBox>(
                view.FindControl<TextBox>("InspectorScaleX"));
            positionX.Text = "1.2";
            rotationY.Text = "365";
            scaleX.Text = "1.234567";
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("1.2", shell.PositionX);
            Assert.Equal("365", shell.RotationDegreesY);
            Assert.Equal("1.234567", shell.ScaleX);

            var apply = Assert.IsType<Button>(view.FindControl<Button>("ApplyTransformButton"));
            Assert.NotNull(apply.Command);
            Assert.NotSame(shell.ApplyEntityTransformCommand, apply.Command);
            apply.Command!.Execute(apply.CommandParameter);
            await WaitUntilAsync(() => requestedTransform.HasValue);
            Dispatcher.UIThread.RunJobs();

            var rotation = requestedTransform!.Value.Rotation;
            Assert.InRange(rotation.X, -1.0e-6F, 1.0e-6F);
            Assert.InRange(rotation.Y, -0.043620F, -0.043618F);
            Assert.InRange(rotation.Z, -1.0e-6F, 1.0e-6F);
            Assert.InRange(rotation.W, -0.999049F, -0.999047F);
            Assert.Equal(2UL, shell.AppliedProjectSnapshot.Document!.Revision);
            Assert.Equal(requestedTransform.Value, shell.SelectedEntity!.Transform);
            Assert.Equal(1.2F, requestedTransform.Value.Position.X);
            Assert.Equal(1.234567F, requestedTransform.Value.Scale.X);
            Assert.Equal("1.2", positionX.Text);
            Assert.Equal("0", rotationX.Text);
            Assert.Equal("365", rotationY.Text);
            Assert.Equal("0", rotationZ.Text);
            Assert.Equal("1.234567", scaleX.Text);
            Assert.Equal("1.2", shell.PositionX);
            Assert.Equal("0", shell.RotationDegreesX);
            Assert.Equal("365", shell.RotationDegreesY);
            Assert.Equal("0", shell.RotationDegreesZ);
            Assert.Equal("1.234567", shell.ScaleX);
            var message = Assert.IsType<TextBlock>(
                view.FindControl<TextBlock>("InspectorOperationMessage"));
            Assert.True(message.IsVisible);
            Assert.Equal("Updated entity Transform.", message.Text);
        }
        finally
        {
            window.Close();
        }
    }

    [AvaloniaFact]
    public void Invalid_rotation_text_is_reported_inside_the_active_inspector()
    {
        var entity = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Directional Wedge",
            TransformValue.Identity,
            SceneMeshReference.DirectionalWedgeValidation);
        using var shell = StudioShellTestFactory.Create(out var projectSession, out _);
        projectSession.Publish(Ready(Guid.NewGuid(), revision: 1, entity));
        shell.MarkReady();
        shell.SelectedEntity = entity;
        var requested = false;
        projectSession.SetTransformHandler = (_, _, _, _) =>
        {
            requested = true;
            throw new InvalidOperationException("Invalid input reached the project session.");
        };
        using var viewModel = new StudioInspectorPanelViewModel(
            shell,
            shell.ProjectAssetCatalog,
            shell.EditorSelection);
        var view = new StudioInspectorPanelView
        {
            DataContext = viewModel,
        };
        var window = new MainWindow
        {
            Width = 420,
            Height = 520,
            Content = view,
        };

        try
        {
            window.Show();
            Dispatcher.UIThread.RunJobs();
            Assert.IsType<TextBox>(
                view.FindControl<TextBox>("InspectorRotationDegreesY")).Text = "not-a-number";
            Dispatcher.UIThread.RunJobs();

            var apply = Assert.IsType<Button>(view.FindControl<Button>("ApplyTransformButton"));
            apply.Command!.Execute(apply.CommandParameter);
            Dispatcher.UIThread.RunJobs();

            Assert.False(requested);
            var message = Assert.IsType<TextBlock>(
                view.FindControl<TextBlock>("InspectorOperationMessage"));
            Assert.True(message.IsVisible);
            Assert.Contains("rotation is expressed in degrees", message.Text);
        }
        finally
        {
            window.Close();
        }
    }

    private static ProjectSessionSnapshot Ready(
        Guid sceneId,
        ulong revision,
        params SceneEntitySnapshot[] entities) =>
        ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                ProjectSessionId.CreateNew(),
                Guid.NewGuid(),
                "Sample",
                "C:\\Projects\\Sample"),
            new SceneDocumentSnapshot(
                sceneId,
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision,
                savedRevision: 1,
                entities),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(1));
        while (!condition())
        {
            Dispatcher.UIThread.RunJobs();
            await Task.Delay(10, timeout.Token);
        }
    }

    private static AssetCatalogSnapshot AssetSnapshot(
        Guid projectId,
        params AssetCatalogEntry[] entries) =>
        new(
            AssetCatalogSnapshotState.Ready,
            revision: 9,
            DateTimeOffset.UtcNow,
            projectId,
            "C:\\Projects\\Sample\\asharia.project.json",
            "C:\\Projects\\Sample\\.asharia\\cache\\assets\\manifest.json",
            "editor-preview",
            [new AssetCatalogSourceRoot(
                "Assets",
                "Assets",
                "Assets",
                "C:\\Projects\\Sample\\Assets")],
            ImmutableArray<AssetCatalogNavigationEntry>.Empty,
            entries.ToImmutableArray(),
            ImmutableArray<AssetCatalogDiagnostic>.Empty);

    private static AssetCatalogEntry AssetEntry(
        Guid guid,
        AssetCatalogProductState productState = AssetCatalogProductState.Current,
        string assetType = "Model") =>
        new(
            new AssetSelectionKey(guid, "Assets/Models/Wedge.glb"),
            guid,
            guid.ToString("D"),
            "Assets/Models/Wedge.glb",
            "Assets",
            "Assets",
            "C:\\Projects\\Sample\\Assets",
            "C:\\Projects\\Sample\\Assets\\Models\\Wedge.glb",
            "C:\\Projects\\Sample\\Assets\\Models\\Wedge.glb.ameta",
            "Wedge",
            ".glb",
            assetType,
            "GlbImporter",
            importerVersion: 1,
            "default",
            "Mesh",
            productState,
            currentProductCount: productState == AssetCatalogProductState.Current ? 1 : 0,
            staleProductCount: productState == AssetCatalogProductState.Stale ? 1 : 0,
            [new AssetCatalogSubAsset("mesh:0", "Body", "Mesh")],
            [new AssetCatalogDiagnostic(
                AssetCatalogDiagnosticSeverity.Warning,
                "MODEL-NORMALS",
                "Assets/Models/Wedge.glb",
                "meshes/0",
                "Normals were generated.")]);

    private sealed class SingleAssetCatalogGateway(AssetCatalogQueryResult result) :
        IAssetCatalogGateway
    {
        public ValueTask<AssetCatalogQueryResult> QueryAsync(
            AssetCatalogQueryScope scope,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(result);
        }
    }
}
