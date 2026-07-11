using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Asharia.Editor.Dialogs;
using CommunityToolkit.Mvvm.Input;
using Editor.UI.ViewModels;

namespace Editor.Shell.ViewModels.Dialogs;

public class EditorDialogHostViewModel : ViewModelBase
{
    private EditorDialogRequest? activeRequest_;
    private IReadOnlyList<EditorDialogButtonViewModel> buttons_ = [];
    private TaskCompletionSource<EditorDialogResult>? completion_;

    public bool IsOpen => ActiveRequest is not null;

    public EditorDialogRequest? ActiveRequest
    {
        get => activeRequest_;
        private set
        {
            if (SetProperty(ref activeRequest_, value))
            {
                OnPropertyChanged(nameof(IsOpen));
                OnPropertyChanged(nameof(Title));
                OnPropertyChanged(nameof(Message));
            }
        }
    }

    public string Title => ActiveRequest?.Title ?? string.Empty;

    public string Message => ActiveRequest?.Message ?? string.Empty;

    public IReadOnlyList<EditorDialogButtonViewModel> Buttons
    {
        get => buttons_;
        private set => SetProperty(ref buttons_, value);
    }

    public Task<EditorDialogResult> ShowAsync(EditorDialogRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (ActiveRequest is not null)
        {
            throw new InvalidOperationException("A dialog is already active.");
        }

        var completion = new TaskCompletionSource<EditorDialogResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        completion_ = completion;
        ActiveRequest = request;
        Buttons = request.Actions
            .Select(action => new EditorDialogButtonViewModel(
                action,
                new RelayCommand(() => Complete(EditorDialogResult.ActionInvoked(action.Id)))))
            .ToArray();
        return completion.Task;
    }

    public bool TrySystemDismiss()
    {
        if (ActiveRequest is null || !ActiveRequest.AllowSystemDismiss)
        {
            return false;
        }

        Complete(EditorDialogResult.SystemDismissed());
        return true;
    }

    private void Complete(EditorDialogResult result)
    {
        var completion = completion_;
        completion_ = null;
        Buttons = [];
        ActiveRequest = null;
        completion?.TrySetResult(result);
    }
}
