using System;

namespace Asharia.Editor.Threading;

public interface IEditorUiDispatcher
{
    bool CheckAccess();

    void Post(Action action);
}
