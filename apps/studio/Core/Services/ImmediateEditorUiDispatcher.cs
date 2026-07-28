using System;
using Asharia.Editor.Threading;

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
