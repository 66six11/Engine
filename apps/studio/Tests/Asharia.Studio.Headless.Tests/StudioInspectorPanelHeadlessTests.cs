using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.TestSupport;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Editor.Shell.ViewModels.Panels;
using Editor.Shell.Views.Panels;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioInspectorPanelHeadlessTests
{
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
        var view = new StudioInspectorPanelView
        {
            DataContext = new StudioInspectorPanelViewModel(shell),
        };
        var window = new Window
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
            rotationY.Text = "365";
            Dispatcher.UIThread.RunJobs();
            Assert.Equal("365", shell.RotationDegreesY);

            var apply = Assert.IsType<Button>(view.FindControl<Button>("ApplyTransformButton"));
            Assert.Same(shell.ApplyEntityTransformCommand, apply.Command);
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
            Assert.Equal("0", rotationX.Text);
            Assert.Equal("365", rotationY.Text);
            Assert.Equal("0", rotationZ.Text);
            Assert.Equal("0", shell.RotationDegreesX);
            Assert.Equal("365", shell.RotationDegreesY);
            Assert.Equal("0", shell.RotationDegreesZ);
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
        var view = new StudioInspectorPanelView
        {
            DataContext = new StudioInspectorPanelViewModel(shell),
        };
        var window = new Window
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
}
