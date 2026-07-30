using System;
using System.Collections.Generic;
using Editor.Shell.Views.Windowing;
using Xunit;

namespace Editor.Tests.Shell.Views.Windowing;

public sealed class EditorDockFloatingWindowRegistryTests
{
    [Fact]
    public void CloseAllCore_attempts_every_window_and_completion_before_reporting_failures()
    {
        var firstFailure = new InvalidOperationException("first close failure");
        var secondFailure = new InvalidOperationException("second close failure");
        var events = new List<string>();

        var exception = Assert.Throws<AggregateException>(
            () => EditorDockFloatingWindowRegistry.CloseAllCore(
                [
                    () =>
                    {
                        events.Add("first");
                        throw firstFailure;
                    },
                    () => events.Add("second"),
                    () =>
                    {
                        events.Add("third");
                        throw secondFailure;
                    },
                ],
                () => events.Add("completed")));

        Assert.Equal(["first", "second", "third", "completed"], events);
        Assert.Collection(
            exception.InnerExceptions,
            item => Assert.Same(firstFailure, item),
            item => Assert.Same(secondFailure, item));
    }
}
