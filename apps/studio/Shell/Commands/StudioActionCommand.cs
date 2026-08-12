using System;
using System.Threading.Tasks;
using System.Windows.Input;
using Asharia.Studio.Application.Actions;

namespace Editor.Shell.Commands;

internal sealed class StudioActionCommand : ICommand
{
    private readonly StudioActionId actionId_;
    private readonly Func<StudioActionId, StudioActionContextSnapshot> captureContext_;
    private readonly Func<StudioActionId, StudioActionContextSnapshot, StudioActionStateEvaluation>
        evaluate_;
    private readonly Func<StudioActionId, StudioActionContextSnapshot,
        ValueTask<StudioActionResult>> execute_;
    private readonly Func<bool> canCapture_;
    private bool isExecuting_;

    public StudioActionCommand(
        StudioActionId actionId,
        Func<StudioActionId, StudioActionContextSnapshot> captureContext,
        Func<StudioActionId, StudioActionContextSnapshot, StudioActionStateEvaluation> evaluate,
        Func<StudioActionId, StudioActionContextSnapshot, ValueTask<StudioActionResult>> execute,
        Func<bool>? canCapture = null)
    {
        if (!actionId.IsValid)
        {
            throw new ArgumentException("Action id must be valid.", nameof(actionId));
        }
        ArgumentNullException.ThrowIfNull(captureContext);
        ArgumentNullException.ThrowIfNull(evaluate);
        ArgumentNullException.ThrowIfNull(execute);
        actionId_ = actionId;
        captureContext_ = captureContext;
        evaluate_ = evaluate;
        execute_ = execute;
        canCapture_ = canCapture ?? (() => true);
    }

    public StudioActionCommand(
        StudioActionId actionId,
        StudioActionInvocationSource source,
        StudioPresentationId topLevelId,
        Func<StudioActionId, StudioActionInvocationSource, StudioPresentationId,
            StudioActionContextSnapshot> captureContext,
        Func<StudioActionId, StudioActionContextSnapshot, StudioActionStateEvaluation> evaluate,
        Func<StudioActionId, StudioActionContextSnapshot,
            ValueTask<StudioActionResult>> execute,
        Func<bool>? canCapture = null)
        : this(
            actionId,
            id => captureContext(id, source, topLevelId),
            evaluate,
            execute,
            canCapture)
    {
        if (!topLevelId.IsValid)
        {
            throw new ArgumentException("Top-level id must be valid.", nameof(topLevelId));
        }
    }

    public event EventHandler? CanExecuteChanged;

    public bool CanExecute(object? parameter)
    {
        if (isExecuting_ || !canCapture_())
        {
            return false;
        }

        var context = parameter as StudioActionContextSnapshot
            ?? captureContext_(actionId_);
        var evaluation = evaluate_(actionId_, context);
        return evaluation.Status == StudioActionStateEvaluationStatus.Evaluated &&
            evaluation.State is { IsVisible: true, IsEnabled: true, IsRunning: false };
    }

    public async void Execute(object? parameter)
    {
        if (isExecuting_ || !canCapture_())
        {
            return;
        }

        var context = parameter as StudioActionContextSnapshot
            ?? captureContext_(actionId_);
        if (!CanExecute(context))
        {
            return;
        }

        isExecuting_ = true;
        RaiseCanExecuteChanged();
        try
        {
            await execute_(actionId_, context);
        }
        finally
        {
            isExecuting_ = false;
            RaiseCanExecuteChanged();
        }
    }

    public void RaiseCanExecuteChanged() =>
        CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
