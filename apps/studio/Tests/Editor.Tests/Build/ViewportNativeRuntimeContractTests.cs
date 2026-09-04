using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using Asharia.Studio.EngineBridge.Viewports.Abi;
using Xunit;

namespace Editor.Tests.Build;

public sealed class ViewportNativeRuntimeContractTests
{
    private static readonly string[] ExpectedRequiredExports =
    [
        "editor_viewport_query_composition_compatibility",
        "editor_viewport_release_compatibility_result",
        "editor_viewport_open_stream_v9",
        "editor_viewport_submit_latest_v9",
        "editor_viewport_try_take_ready_v9",
        "editor_viewport_complete_frame_v9",
        "editor_viewport_release_slot_import_v9",
        "editor_viewport_close_stream_v9",
        "editor_viewport_poll_stream_v9",
        "editor_viewport_destroy_stream_v9",
        "editor_viewport_shutdown",
    ];
    private static readonly string[] ExpectedForbiddenExports =
    [
        "editor_viewport_acquire_present_packet",
        "editor_viewport_release_present_packet",
        "editor_viewport_acquire_present_packet_v2",
        "editor_viewport_create_present_slot_v3",
        "editor_viewport_render_present_slot_v3",
        "editor_viewport_create_present_slot_v4",
        "editor_viewport_open_stream_v5",
        "editor_viewport_submit_latest_v5",
        "editor_viewport_try_take_ready_v5",
        "editor_viewport_complete_frame_v5",
        "editor_viewport_release_slot_import_v5",
        "editor_viewport_close_stream_v5",
        "editor_viewport_poll_stream_v5",
        "editor_viewport_destroy_stream_v5",
        "editor_viewport_open_stream_v6",
        "editor_viewport_submit_latest_v6",
        "editor_viewport_try_take_ready_v6",
        "editor_viewport_complete_frame_v6",
        "editor_viewport_release_slot_import_v6",
        "editor_viewport_close_stream_v6",
        "editor_viewport_poll_stream_v6",
        "editor_viewport_destroy_stream_v6",
        "editor_viewport_open_stream_v7",
        "editor_viewport_submit_latest_v7",
        "editor_viewport_try_take_ready_v7",
        "editor_viewport_complete_frame_v7",
        "editor_viewport_release_slot_import_v7",
        "editor_viewport_close_stream_v7",
        "editor_viewport_poll_stream_v7",
        "editor_viewport_destroy_stream_v7",
        "editor_viewport_open_stream_v8",
        "editor_viewport_submit_latest_v8",
        "editor_viewport_try_take_ready_v8",
        "editor_viewport_complete_frame_v8",
        "editor_viewport_release_slot_import_v8",
        "editor_viewport_close_stream_v8",
        "editor_viewport_poll_stream_v8",
        "editor_viewport_destroy_stream_v8",
    ];

    [Fact]
    public void Built_Editor_runtime_satisfies_the_native_contract()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        var result = ViewportNativeRuntimeContract.Inspect(Path.Combine(
            AppContext.BaseDirectory,
            "editor_native.dll"));

        Assert.True(result.Succeeded, result.Diagnostic);
    }

    [Fact]
    public void Current_only_export_set_satisfies_the_runtime_contract()
    {
        Assert.Equal(ExpectedRequiredExports, ViewportNativeRuntimeContract.RequiredExports);
        Assert.Equal(ExpectedForbiddenExports, ViewportNativeRuntimeContract.ForbiddenExports);
        var probe = new StubExportProbe(ExpectedRequiredExports);

        var result = ViewportNativeRuntimeContract.Inspect("editor_native.dll", probe);

        Assert.True(result.Succeeded, result.Diagnostic);
        Assert.Equal(Path.GetFullPath("editor_native.dll"), result.AbsoluteLibraryPath);
        Assert.Empty(result.MissingRequiredExports);
        Assert.Empty(result.PresentForbiddenExports);
        Assert.Null(result.InspectionError);
    }

    [Fact]
    public void Required_exports_exactly_match_the_managed_library_imports()
    {
        var importedExports = typeof(ViewportNativeEntryPoints)
            .GetMethods(BindingFlags.NonPublic | BindingFlags.Static)
            .Select(method => method.GetCustomAttribute<LibraryImportAttribute>())
            .Where(attribute => attribute is not null)
            .Select(attribute => attribute!.EntryPoint)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(
            ExpectedRequiredExports.Order(StringComparer.Ordinal),
            importedExports);
    }

    [Theory]
    [MemberData(nameof(RequiredExportNames))]
    public void Every_missing_current_export_fails_closed(string missingExport)
    {
        var presentExports = ExpectedRequiredExports
            .Where(exportName => exportName != missingExport)
            .ToArray();

        var result = ViewportNativeRuntimeContract.Inspect(
            "editor_native.dll",
            new StubExportProbe(presentExports));

        Assert.False(result.Succeeded);
        Assert.Equal([missingExport], result.MissingRequiredExports);
        Assert.Empty(result.PresentForbiddenExports);
        Assert.Contains(result.AbsoluteLibraryPath, result.Diagnostic, StringComparison.Ordinal);
        Assert.Contains(missingExport, result.Diagnostic, StringComparison.Ordinal);
    }

    [Theory]
    [MemberData(nameof(ForbiddenExportNames))]
    public void Every_forbidden_export_fails_closed(string forbiddenExport)
    {
        var presentExports = ExpectedRequiredExports
            .Append(forbiddenExport)
            .ToArray();

        var result = ViewportNativeRuntimeContract.Inspect(
            "editor_native.dll",
            new StubExportProbe(presentExports));

        Assert.False(result.Succeeded);
        Assert.Empty(result.MissingRequiredExports);
        Assert.Equal([forbiddenExport], result.PresentForbiddenExports);
        Assert.Contains(result.AbsoluteLibraryPath, result.Diagnostic, StringComparison.Ordinal);
        Assert.Contains(forbiddenExport, result.Diagnostic, StringComparison.Ordinal);
    }

    [Fact]
    public void Probe_failure_preserves_the_absolute_library_path_and_reason()
    {
        const string error = "BadImageFormatException: invalid PE image";

        var result = ViewportNativeRuntimeContract.Inspect(
            "editor_native.dll",
            new StubExportProbe([], error));

        Assert.False(result.Succeeded);
        Assert.Equal(error, result.InspectionError);
        Assert.Contains(result.AbsoluteLibraryPath, result.Diagnostic, StringComparison.Ordinal);
        Assert.Contains(error, result.Diagnostic, StringComparison.Ordinal);
    }

    public static IEnumerable<object[]> RequiredExportNames() =>
        ExpectedRequiredExports.Select(exportName => new object[] { exportName });

    public static IEnumerable<object[]> ForbiddenExportNames() =>
        ExpectedForbiddenExports.Select(exportName => new object[] { exportName });

    private sealed class StubExportProbe : IViewportNativeExportProbe
    {
        private readonly IReadOnlySet<string> presentExports_;
        private readonly string? error_;

        public StubExportProbe(IEnumerable<string> presentExports, string? error = null)
        {
            presentExports_ = presentExports.ToHashSet(StringComparer.Ordinal);
            error_ = error;
        }

        public ViewportNativeExportProbeResult Inspect(
            string absoluteLibraryPath,
            IReadOnlyList<string> exportNames)
        {
            Assert.True(Path.IsPathFullyQualified(absoluteLibraryPath));
            Assert.Equal(
                ExpectedRequiredExports.Concat(ExpectedForbiddenExports),
                exportNames);
            return new ViewportNativeExportProbeResult(presentExports_, error_);
        }
    }
}
