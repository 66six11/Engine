using System;

namespace Editor.Shell.Services;

internal interface IEditorUiDispatcher
{
    bool CheckAccess();

    void Post(Action action);
}
