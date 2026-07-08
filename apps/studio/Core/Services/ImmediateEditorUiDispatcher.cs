using System;
using Editor.Core.Abstractions;

namespace Editor.Core.Services;

internal sealed class ImmediateEditorUiDispatcher : IEditorUiDispatcher
{
    public bool CheckAccess() => true;

    public void Post(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);

        action();
    }
}
