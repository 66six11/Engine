using System;
using System.Linq;
using Asharia.Editor.Diagnostics.FrameDebug;
using Xunit;

namespace Asharia.Editor.Tests.Diagnostics.FrameDebug;

public sealed class FrameDebugContractTests
{
    [Fact]
    public void Frame_debug_contracts_are_owned_by_public_editor_api()
    {
        var types = new[]
        {
            typeof(FrameDebugAccessEdgeSnapshot),
            typeof(FrameDebugCaptureSnapshot),
            typeof(FrameDebugCommandSnapshot),
            typeof(FrameDebugDependencyEdgeSnapshot),
            typeof(FrameDebugExecutionEventSnapshot),
            typeof(FrameDebugPassSnapshot),
            typeof(FrameDebugPreviewSnapshot),
            typeof(FrameDebugResourceSnapshot),
            typeof(FrameDebugTransitionSnapshot),
            typeof(FrameDebuggerSnapshot),
            typeof(FrameDebuggerState),
            typeof(IFrameDebuggerSnapshotProvider),
        };

        Assert.All(types, type => Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name));
        Assert.All(types, type => Assert.Equal("Asharia.Editor.Diagnostics.FrameDebug", type.Namespace));
    }

    [Fact]
    public void Public_frame_debug_api_excludes_native_transport_types()
    {
        var exportedNames = typeof(FrameDebuggerSnapshot).Assembly
            .GetExportedTypes()
            .Where(type => type.Namespace == "Asharia.Editor.Diagnostics.FrameDebug")
            .Select(type => type.Name)
            .ToArray();

        Assert.DoesNotContain(exportedNames, name => name.Contains("Native", StringComparison.Ordinal));
        Assert.DoesNotContain(exportedNames, name => name.Contains("Payload", StringComparison.Ordinal));
    }
}
