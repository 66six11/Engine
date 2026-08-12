using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.TestSupport;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Avalonia.VisualTree;
using Editor.Shell.Services.Projects;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class MainWindowDocumentTransitionPromptTests
{
    [AvaloniaFact]
    public async Task Prompt_exposes_explicit_save_discard_and_cancel_results()
    {
        var owner = new Window { Width = 640, Height = 480 };
        owner.Show();

        try
        {
            await AssertDecisionAsync(
                owner,
                "StudioUnsavedDocumentSave",
                ProjectDocumentTransitionDecision.Save);
            await AssertDecisionAsync(
                owner,
                "StudioUnsavedDocumentDiscard",
                ProjectDocumentTransitionDecision.Discard);
            await AssertDecisionAsync(
                owner,
                "StudioUnsavedDocumentCancel",
                ProjectDocumentTransitionDecision.Cancel);
        }
        finally
        {
            owner.Close();
        }
    }

    [AvaloniaFact]
    public async Task Prompt_projects_document_identity_and_accessibility_metadata()
    {
        var dialog = MainWindowDocumentTransitionPrompt.CreateDialog(
            await CreatePromptAsync());
        dialog.Show();
        Dispatcher.UIThread.RunJobs();

        try
        {
            Assert.Equal(
                "StudioUnsavedDocumentPrompt",
                AutomationProperties.GetAutomationId(dialog));
            Assert.Equal(
                "Unsaved document confirmation",
                AutomationProperties.GetName(dialog));
            var text = dialog.GetVisualDescendants()
                .OfType<TextBlock>()
                .Select(block => block.Text)
                .ToArray();
            Assert.Contains("Save changes before continuing?", text);
            Assert.Contains(
                "Default.asharia.scene.json in Sample has unsaved changes.",
                text);
            Assert.Contains(
                dialog.GetVisualDescendants().OfType<Button>(),
                button => button.IsCancel &&
                    AutomationProperties.GetAutomationId(button) ==
                        "StudioUnsavedDocumentCancel");
            Assert.Contains(
                dialog.GetVisualDescendants().OfType<Button>(),
                button => button.IsDefault &&
                    AutomationProperties.GetAutomationId(button) ==
                        "StudioUnsavedDocumentSave");
        }
        finally
        {
            dialog.Close();
        }
    }

    private static async Task AssertDecisionAsync(
        Window owner,
        string automationId,
        ProjectDocumentTransitionDecision expected)
    {
        var dialog = MainWindowDocumentTransitionPrompt.CreateDialog(
            await CreatePromptAsync());
        var completion = dialog.ShowDialog<ProjectDocumentTransitionDecision>(owner);
        Dispatcher.UIThread.RunJobs();
        var button = dialog.GetVisualDescendants()
            .OfType<Button>()
            .Single(candidate =>
                AutomationProperties.GetAutomationId(candidate) == automationId);

        button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Dispatcher.UIThread.RunJobs();

        Assert.Equal(expected, await completion);
    }

    private static async Task<ProjectDocumentTransitionPrompt> CreatePromptAsync()
    {
        var snapshot = ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                new ProjectSessionId(Guid.Parse(
                    "12345678-1234-1234-1234-123456789abc")),
                Guid.Parse("87654321-4321-4321-4321-cba987654321"),
                "Sample",
                "C:\\Projects\\Sample"),
            new Asharia.Studio.Application.Scenes.SceneDocumentSnapshot(
                Guid.Parse("11111111-2222-3333-4444-555555555555"),
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision: 2,
                savedRevision: 1,
                entities: []),
            new ContentStateId(2),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var session = new TestProjectSession();
        session.Publish(snapshot);
        var capture = new CapturingPrompt();
        var coordinator = new ProjectDocumentTransitionCoordinator(session, capture);

        var result = await coordinator.PrepareExitAsync();

        Assert.Equal(ProjectDocumentTransitionStatus.Cancelled, result.Status);
        return Assert.IsType<ProjectDocumentTransitionPrompt>(capture.Prompt);
    }

    private sealed class CapturingPrompt : IProjectDocumentTransitionPrompt
    {
        public ProjectDocumentTransitionPrompt? Prompt { get; private set; }

        public ValueTask<ProjectDocumentTransitionDecision> ChooseAsync(
            ProjectDocumentTransitionPrompt prompt,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Prompt = prompt;
            return ValueTask.FromResult(ProjectDocumentTransitionDecision.Cancel);
        }
    }
}
