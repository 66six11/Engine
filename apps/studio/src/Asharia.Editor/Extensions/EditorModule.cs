using System;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Editor.Extensions;

public abstract class EditorModule
{
    public abstract void Configure(EditorModuleBuilder editor);

    public virtual ValueTask<IEditorModuleActivation> ActivateAsync(
        EditorModuleContext context,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(context);
        return ValueTask.FromResult(EditorModuleActivation.Empty);
    }
}
