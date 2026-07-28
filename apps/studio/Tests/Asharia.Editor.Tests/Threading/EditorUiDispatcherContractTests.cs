using System;
using System.Linq;
using Asharia.Editor.Threading;
using Xunit;

namespace Asharia.Editor.Tests.Threading;

public sealed class EditorUiDispatcherContractTests
{
    [Fact]
    public void Ui_dispatcher_contract_is_owned_by_public_editor_api()
    {
        var type = typeof(IEditorUiDispatcher);

        Assert.True(type.IsPublic);
        Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name);
        Assert.Equal("Asharia.Editor.Threading", type.Namespace);
        Assert.Equal(typeof(bool), type.GetMethod(nameof(IEditorUiDispatcher.CheckAccess))?.ReturnType);
        Assert.Equal(
            [typeof(Action)],
            type.GetMethod(nameof(IEditorUiDispatcher.Post))?
                .GetParameters()
                .Select(parameter => parameter.ParameterType));
    }
}
