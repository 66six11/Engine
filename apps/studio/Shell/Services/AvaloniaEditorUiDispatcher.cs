using System;
using Avalonia.Threading;
using Editor.Core.Abstractions;

namespace Editor.Shell.Services;

internal sealed class AvaloniaEditorUiDispatcher : IEditorUiDispatcher
{
    public bool CheckAccess() => Dispatcher.UIThread.CheckAccess();

    public void Post(Action action)
    {
        Dispatcher.UIThread.Post(action);
    }
}
