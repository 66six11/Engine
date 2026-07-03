using Editor.Core.Services;
using Xunit;

namespace Editor.Tests.Core;

public sealed class ImmediateEditorUiDispatcherTests
{
    [Fact]
    public void Immediate_dispatcher_reports_access_and_runs_posted_action()
    {
        var dispatcher = new ImmediateEditorUiDispatcher();
        var invoked = false;

        Assert.True(dispatcher.CheckAccess());
        dispatcher.Post(() => invoked = true);

        Assert.True(invoked);
    }
}
