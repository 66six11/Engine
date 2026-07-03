using Editor.Core.Models;
using Editor.Core.Models.Workbench;

namespace Editor.Core.CodeFirstUI;

public interface IEditorGuiCommandExecutor
{
    WorkbenchCommandExecutionResult Execute(string commandId);
}
