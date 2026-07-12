using System;
using Asharia.Editor.Commands;
using Asharia.Editor.UI.CodeFirst.Abstractions;
using Asharia.Studio.Application.Commands;
using Xunit;

namespace Asharia.Studio.Application.Tests.Commands;

public sealed class EditorCommandStatusMessageRouterTests
{
    [Fact]
    public void Execute_returns_inner_result_and_publishes_same_instance()
    {
        var expected = EditorCommandExecutionResult.Success("studio.command");
        var inner = new StubCommandExecutor(expected);
        EditorCommandExecutionResult? published = null;
        var router = new EditorCommandStatusMessageRouter(inner, result => published = result);

        var actual = router.Execute("studio.command");

        Assert.Same(expected, actual);
        Assert.Same(expected, published);
        Assert.Equal("studio.command", inner.LastCommandId);
    }

    [Fact]
    public void Constructor_rejects_null_inner_executor()
    {
        Assert.Throws<ArgumentNullException>(
            () => new EditorCommandStatusMessageRouter(null!, _ => { }));
    }

    [Fact]
    public void Constructor_rejects_null_publisher()
    {
        Assert.Throws<ArgumentNullException>(
            () => new EditorCommandStatusMessageRouter(
                new StubCommandExecutor(EditorCommandExecutionResult.NotFound("missing")),
                null!));
    }

    [Fact]
    public void Execute_preserves_inner_exception_and_does_not_publish_a_result()
    {
        var expected = new InvalidOperationException("command failed");
        var published = false;
        var router = new EditorCommandStatusMessageRouter(
            new ThrowingCommandExecutor(expected),
            _ => published = true);

        var actual = Assert.Throws<InvalidOperationException>(
            () => router.Execute("studio.command"));

        Assert.Same(expected, actual);
        Assert.False(published);
    }

    private sealed class StubCommandExecutor(EditorCommandExecutionResult result) :
        IEditorGuiCommandExecutor
    {
        public string? LastCommandId { get; private set; }

        public EditorCommandExecutionResult Execute(string commandId)
        {
            LastCommandId = commandId;
            return result;
        }
    }

    private sealed class ThrowingCommandExecutor(Exception exception) : IEditorGuiCommandExecutor
    {
        public EditorCommandExecutionResult Execute(string commandId)
        {
            throw exception;
        }
    }
}
