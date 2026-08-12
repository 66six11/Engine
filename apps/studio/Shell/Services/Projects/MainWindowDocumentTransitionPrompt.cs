using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;
using Editor.Shell.Views.Windowing;

namespace Editor.Shell.Services.Projects;

internal sealed class MainWindowDocumentTransitionPrompt :
    IProjectDocumentTransitionPrompt
{
    private MainWindow? owner_;

    public void Attach(MainWindow owner)
    {
        ArgumentNullException.ThrowIfNull(owner);
        if (owner_ is not null)
        {
            throw new InvalidOperationException(
                "The document transition prompt already has an owner window.");
        }

        owner_ = owner;
    }

    public async ValueTask<ProjectDocumentTransitionDecision> ChooseAsync(
        ProjectDocumentTransitionPrompt prompt,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(prompt);
        cancellationToken.ThrowIfCancellationRequested();
        if (!Dispatcher.UIThread.CheckAccess())
        {
            throw new InvalidOperationException(
                "The document transition prompt must be shown on the UI thread.");
        }

        var dialog = CreateDialog(prompt);
        using var registration = cancellationToken.Register(
            static state => Dispatcher.UIThread.Post(() => ((Window)state!).Close()),
            dialog);
        var decision = await dialog.ShowDialog<ProjectDocumentTransitionDecision>(
            RequireOwner());
        cancellationToken.ThrowIfCancellationRequested();
        return decision;
    }

    internal static Window CreateDialog(ProjectDocumentTransitionPrompt prompt)
    {
        ArgumentNullException.ThrowIfNull(prompt);
        var dialog = new Window
        {
            Title = "Unsaved Changes",
            Width = 460,
            MinWidth = 380,
            SizeToContent = SizeToContent.Height,
            CanResize = false,
            ShowInTaskbar = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
        };
        AutomationProperties.SetAutomationId(dialog, "StudioUnsavedDocumentPrompt");
        AutomationProperties.SetName(dialog, "Unsaved document confirmation");

        var save = CreateButton(
            "Save",
            "StudioUnsavedDocumentSave",
            () => dialog.Close(ProjectDocumentTransitionDecision.Save));
        save.IsDefault = true;
        var discard = CreateButton(
            "Discard",
            "StudioUnsavedDocumentDiscard",
            () => dialog.Close(ProjectDocumentTransitionDecision.Discard));
        var cancel = CreateButton(
            "Cancel",
            "StudioUnsavedDocumentCancel",
            () => dialog.Close(ProjectDocumentTransitionDecision.Cancel));
        cancel.IsCancel = true;

        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Spacing = 8,
            Children =
            {
                save,
                discard,
                cancel,
            },
        };
        var documentName = Path.GetFileName(prompt.DocumentPath);
        var panel = new StackPanel
        {
            Spacing = 12,
            Children =
            {
                new TextBlock
                {
                    FontSize = 18,
                    FontWeight = Avalonia.Media.FontWeight.SemiBold,
                    Text = "Save changes before continuing?",
                },
                new TextBlock
                {
                    Text = $"{documentName} in {prompt.ProjectName} has unsaved changes.",
                    TextWrapping = Avalonia.Media.TextWrapping.Wrap,
                },
                new TextBlock
                {
                    Text = "Discard permanently loses the unsaved document changes.",
                    TextWrapping = Avalonia.Media.TextWrapping.Wrap,
                },
                buttons,
            },
        };
        dialog.Content = new Border
        {
            Padding = new Thickness(20),
            Child = panel,
        };
        return dialog;
    }

    private MainWindow RequireOwner() =>
        owner_ ?? throw new InvalidOperationException(
            "The document transition prompt has no owner window.");

    private static Button CreateButton(
        string label,
        string automationId,
        Action action)
    {
        var button = new Button
        {
            Content = label,
            MinWidth = 80,
        };
        AutomationProperties.SetAutomationId(button, automationId);
        AutomationProperties.SetName(button, label);
        button.Click += (_, _) => action();
        return button;
    }
}
