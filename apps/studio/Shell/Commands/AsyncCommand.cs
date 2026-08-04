using System;
using System.Threading.Tasks;
using System.Windows.Input;

namespace Editor.Shell.Commands;

internal sealed class AsyncCommand : ICommand
{
    private readonly Func<Task> execute_;
    private readonly Func<bool> canExecute_;
    private bool isExecuting_;

    public AsyncCommand(Func<Task> execute, Func<bool> canExecute)
    {
        ArgumentNullException.ThrowIfNull(execute);
        ArgumentNullException.ThrowIfNull(canExecute);
        execute_ = execute;
        canExecute_ = canExecute;
    }

    public event EventHandler? CanExecuteChanged;

    public bool CanExecute(object? parameter) =>
        !isExecuting_ && canExecute_();

    public async void Execute(object? parameter)
    {
        if (!CanExecute(parameter))
        {
            return;
        }

        isExecuting_ = true;
        RaiseCanExecuteChanged();
        try
        {
            await execute_();
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
