using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Avalonia.Logging;
using Editor.Shell.Diagnostics;
using Xunit;

namespace Editor.Tests.Shell.Diagnostics;

public sealed class StudioAvaloniaLogSinkTests
{
    [Fact]
    public void Sink_maps_enabled_framework_log_to_bounded_structured_hub()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 4);
        var sink = new StudioAvaloniaLogSink(hub);

        sink.Log(
            LogEventLevel.Information,
            "Binding",
            this,
            "Filtered {Value}",
            "value");
        sink.Log(
            LogEventLevel.Warning,
            "Binding",
            this,
            "Binding failed for {Control}.",
            "Button");

        var record = Assert.Single(hub.ReadLogs(maxCount: 4).Items);
        Assert.Equal(StudioLogLevel.Warning, record.Level);
        Assert.Equal("Binding", record.Channel);
        Assert.Equal(StudioRecordOrigin.Framework, record.Context.Origin);
        Assert.Equal("avalonia", record.Context.Package);
        Assert.Equal(GetType().FullName, record.Source);
        Assert.Equal("Binding failed for Button.", record.RenderedMessage);
        Assert.Contains(
            record.Attributes,
            attribute => attribute.Name == "arg0" && attribute.Value == "Button");
    }

    [Fact]
    public void Sink_does_not_invoke_arbitrary_framework_value_code()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var sink = new StudioAvaloniaLogSink(hub, LogEventLevel.Verbose);
        var value = new ThrowingValue();

        var exception = Record.Exception(() => sink.Log(
            LogEventLevel.Error,
            "Layout",
            source: null,
            "Bad property {Value}",
            value));

        Assert.Null(exception);
        Assert.Equal(0, value.CallCount);
        var record = Assert.Single(hub.ReadLogs(maxCount: 2).Items);
        var marker = $"<{typeof(ThrowingValue).FullName}>";
        Assert.Equal($"Bad property {marker}", record.RenderedMessage);
        Assert.Contains(
            record.Attributes,
            attribute => attribute.Name == "arg0"
                && attribute.Value == marker);
    }

    [Fact]
    public async Task Sink_returns_without_waiting_for_a_blocking_framework_value()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var sink = new StudioAvaloniaLogSink(hub, LogEventLevel.Verbose);
        using var release = new ManualResetEventSlim();
        var value = new BlockingValue(release);
        var publish = Task.Run(() => sink.Log(
            LogEventLevel.Error,
            "Layout",
            source: null,
            "Blocked property {Value}",
            value));
        try
        {
            await publish.WaitAsync(TimeSpan.FromSeconds(1));
        }
        finally
        {
            release.Set();
            await publish.WaitAsync(TimeSpan.FromSeconds(5));
        }

        Assert.Equal(0, value.CallCount);
        var record = Assert.Single(hub.ReadLogs(maxCount: 2).Items);
        Assert.Equal(
            $"Blocked property <{typeof(BlockingValue).FullName}>",
            record.RenderedMessage);
    }

    [Fact]
    public void Sink_formats_bounded_scalar_values_once_for_attributes_and_rendering()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var sink = new StudioAvaloniaLogSink(hub, LogEventLevel.Verbose);
        var values = new object?[17];
        for (var index = 0; index < values.Length; index++)
        {
            values[index] = index + 0.5;
        }

        sink.Log(
            LogEventLevel.Error,
            "Layout",
            source: null,
            string.Join(' ', new string[17].Select((_, index) => $"{{Value{index}}}")),
            values);

        var record = Assert.Single(hub.ReadLogs(maxCount: 2).Items);
        Assert.Equal(16, record.Attributes.Length);
        Assert.Equal("0.5", record.Attributes[0].Value);
        Assert.Equal("15.5", record.Attributes[15].Value);
        Assert.Contains("0.5", record.RenderedMessage, StringComparison.Ordinal);
        Assert.Contains("15.5", record.RenderedMessage, StringComparison.Ordinal);
        Assert.DoesNotContain("16.5", record.RenderedMessage, StringComparison.Ordinal);
    }

    private sealed class ThrowingValue
    {
        public int CallCount { get; private set; }

        public override string ToString()
        {
            CallCount++;
            throw new InvalidOperationException("Rendering failed.");
        }
    }

    private sealed class BlockingValue(ManualResetEventSlim release)
    {
        public int CallCount { get; private set; }

        public override string ToString()
        {
            CallCount++;
            release.Wait();
            return "released";
        }
    }
}
