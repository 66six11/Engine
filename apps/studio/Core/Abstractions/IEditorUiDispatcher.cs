using System;

namespace Editor.Core.Abstractions;

internal interface IEditorUiDispatcher
{
    bool CheckAccess();

    void Post(Action action);
}
