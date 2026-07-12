using System;
using System.Collections.Generic;
using Asharia.Editor.Diagnostics;
using Asharia.Studio.Application.Diagnostics;
using Xunit;

namespace Asharia.Studio.Application.Tests.Diagnostics;

public sealed class EditorDiagnosticServiceTests
{
    [Fact]
    public void Publish_records_latest_debug_diagnostic_and_raises_change()
    {
        var service = new EditorDiagnosticService(capacity: 2);
        var changedCount = 0;
        service.DiagnosticsChanged += (_, _) => changedCount++;

        var record = service.Publish(
            EditorDiagnosticSeverity.Info,
            EditorDiagnosticChannel.Debug,
            "command",
            "workbench",
            "Command completed.");

        Assert.Equal(1, record.SequenceId);
        Assert.Equal(record, service.GetLatestDiagnostic());
        Assert.Equal([record], service.GetRecentDiagnostics());
        Assert.Equal(1, changedCount);
    }

    [Fact]
    public void Publish_keeps_bounded_recent_history()
    {
        var service = new EditorDiagnosticService(capacity: 2);

        service.Publish(EditorDiagnosticSeverity.Info, EditorDiagnosticChannel.Debug, "one", "test", "One");
        var second = service.Publish(EditorDiagnosticSeverity.Warning, EditorDiagnosticChannel.Debug, "two", "test", "Two");
        var third = service.Publish(EditorDiagnosticSeverity.Error, EditorDiagnosticChannel.Problem, "three", "test", "Three");

        Assert.Equal([second, third], service.GetRecentDiagnostics());
    }

    [Fact]
    public void GetProblemDiagnostics_returns_problem_channel_records_only()
    {
        var service = new EditorDiagnosticService();
        service.Publish(EditorDiagnosticSeverity.Info, EditorDiagnosticChannel.Debug, "debug", "command", "Debug");
        var problem = service.Publish(
            EditorDiagnosticSeverity.Error,
            EditorDiagnosticChannel.Problem,
            "validation",
            "scene",
            "Problem");

        Assert.Equal([problem], service.GetProblemDiagnostics());
    }

    [Fact]
    public void DiagnosticsChanged_observes_committed_record()
    {
        var service = new EditorDiagnosticService();
        IReadOnlyList<EditorDiagnosticRecord> observedRecords = [];
        service.DiagnosticsChanged += (_, _) => observedRecords = service.GetRecentDiagnostics();

        var record = service.Publish(
            EditorDiagnosticSeverity.Warning,
            EditorDiagnosticChannel.Problem,
            "validation",
            "scene",
            "Warning");

        Assert.Equal([record], observedRecords);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public void Publish_rejects_empty_source_category_or_message(string empty)
    {
        var service = new EditorDiagnosticService();

        Assert.Throws<ArgumentException>(() =>
            service.Publish(EditorDiagnosticSeverity.Info, EditorDiagnosticChannel.Debug, empty, "category", "Message"));
        Assert.Throws<ArgumentException>(() =>
            service.Publish(EditorDiagnosticSeverity.Info, EditorDiagnosticChannel.Debug, "source", empty, "Message"));
        Assert.Throws<ArgumentException>(() =>
            service.Publish(EditorDiagnosticSeverity.Info, EditorDiagnosticChannel.Debug, "source", "category", empty));
    }

    [Fact]
    public void Constructor_rejects_non_positive_capacity()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new EditorDiagnosticService(capacity: 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => new EditorDiagnosticService(capacity: -1));
    }
}
