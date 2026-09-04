using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security;

namespace Asharia.Studio.EngineBridge.Viewports.Abi;

internal interface IViewportNativeExportProbe
{
    ViewportNativeExportProbeResult Inspect(
        string absoluteLibraryPath,
        IReadOnlyList<string> exportNames);
}

internal sealed record ViewportNativeExportProbeResult(
    IReadOnlySet<string> PresentExports,
    string? Error);

internal sealed record ViewportNativeRuntimeContractResult(
    bool Succeeded,
    string AbsoluteLibraryPath,
    IReadOnlyList<string> MissingRequiredExports,
    IReadOnlyList<string> PresentForbiddenExports,
    string? InspectionError)
{
    public string Diagnostic
    {
        get
        {
            if (Succeeded)
            {
                return $"Native viewport ABI contract is valid at '{AbsoluteLibraryPath}'.";
            }

            var failures = new List<string>();
            if (!string.IsNullOrWhiteSpace(InspectionError))
            {
                failures.Add($"inspection failed ({InspectionError})");
            }
            if (MissingRequiredExports.Count != 0)
            {
                failures.Add(
                    $"missing required exports: {string.Join(", ", MissingRequiredExports)}");
            }
            if (PresentForbiddenExports.Count != 0)
            {
                failures.Add(
                    $"disallowed exports are present: " +
                    string.Join(", ", PresentForbiddenExports));
            }

            return $"Native viewport ABI contract failed for '{AbsoluteLibraryPath}': " +
                string.Join("; ", failures) + ".";
        }
    }
}

internal static class ViewportNativeRuntimeContract
{
    internal static IReadOnlyList<string> RequiredExports { get; } =
    [
        ViewportNativeEntryPoints.QueryCompositionCompatibilityExport,
        ViewportNativeEntryPoints.ReleaseCompatibilityResultExport,
        ViewportNativeEntryPoints.OpenStreamV8Export,
        ViewportNativeEntryPoints.SubmitLatestV8Export,
        ViewportNativeEntryPoints.TryTakeReadyV8Export,
        ViewportNativeEntryPoints.CompleteFrameV8Export,
        ViewportNativeEntryPoints.ReleaseSlotImportV8Export,
        ViewportNativeEntryPoints.CloseStreamV8Export,
        ViewportNativeEntryPoints.PollStreamV8Export,
        ViewportNativeEntryPoints.DestroyStreamV8Export,
        ViewportNativeEntryPoints.ShutdownExport,
    ];

    internal static IReadOnlyList<string> ForbiddenExports { get; } =
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
    ];

    private static readonly IReadOnlyList<string> InspectedExports =
        [.. RequiredExports, .. ForbiddenExports];

    internal static ViewportNativeRuntimeContractResult Inspect(string libraryPath) =>
        Inspect(libraryPath, NativeLibraryViewportExportProbe.Instance);

    internal static ViewportNativeRuntimeContractResult Inspect(
        string libraryPath,
        IViewportNativeExportProbe probe)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(libraryPath);
        ArgumentNullException.ThrowIfNull(probe);

        var absoluteLibraryPath = Path.GetFullPath(libraryPath);
        var inspection = probe.Inspect(absoluteLibraryPath, InspectedExports);
        if (!string.IsNullOrWhiteSpace(inspection.Error))
        {
            return new ViewportNativeRuntimeContractResult(
                false,
                absoluteLibraryPath,
                [],
                [],
                inspection.Error);
        }

        var missingRequired = RequiredExports
            .Where(exportName => !inspection.PresentExports.Contains(exportName))
            .ToArray();
        var presentForbidden = ForbiddenExports
            .Where(inspection.PresentExports.Contains)
            .ToArray();
        return new ViewportNativeRuntimeContractResult(
            missingRequired.Length == 0 && presentForbidden.Length == 0,
            absoluteLibraryPath,
            missingRequired,
            presentForbidden,
            null);
    }
}

internal sealed class NativeLibraryViewportExportProbe : IViewportNativeExportProbe
{
    public static NativeLibraryViewportExportProbe Instance { get; } = new();

    private NativeLibraryViewportExportProbe()
    {
    }

    public ViewportNativeExportProbeResult Inspect(
        string absoluteLibraryPath,
        IReadOnlyList<string> exportNames)
    {
        nint library = 0;
        try
        {
            library = NativeLibrary.Load(absoluteLibraryPath);
            var presentExports = exportNames
                .Where(exportName => NativeLibrary.TryGetExport(library, exportName, out _))
                .ToHashSet(StringComparer.Ordinal);
            return new ViewportNativeExportProbeResult(presentExports, null);
        }
        catch (Exception exception) when (
            exception is DllNotFoundException or
                BadImageFormatException or
                FileLoadException or
                IOException or
                UnauthorizedAccessException or
                SecurityException)
        {
            return new ViewportNativeExportProbeResult(
                new HashSet<string>(StringComparer.Ordinal),
                $"{exception.GetType().Name}: {exception.Message}");
        }
        finally
        {
            if (library != 0)
            {
                NativeLibrary.Free(library);
            }
        }
    }
}
