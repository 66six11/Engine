using System;
using System.Collections.Generic;
using System.Linq;
using Asharia.Editor.Viewports;

namespace Editor.Core.Models.Viewports;

public sealed record ViewportCompositionCapabilitiesSnapshot
{
    public ViewportCompositionCapabilitiesSnapshot(
        ViewportId viewportId,
        ViewportCompositionStatus status,
        string? deviceLuid,
        string? deviceUuid,
        IReadOnlyList<string> imageHandleTypes,
        IReadOnlyList<string> semaphoreHandleTypes,
        IReadOnlyList<string> synchronizationCapabilities,
        string message,
        DateTimeOffset capturedAtUtc)
    {
        if (viewportId.IsDefault)
        {
            throw new ArgumentException(
                "Viewport id must be initialized.",
                nameof(viewportId));
        }

        if (!Enum.IsDefined(status))
        {
            throw new ArgumentOutOfRangeException(
                nameof(status),
                status,
                "Viewport composition status is not defined.");
        }

        ArgumentNullException.ThrowIfNull(imageHandleTypes);
        ArgumentNullException.ThrowIfNull(semaphoreHandleTypes);
        ArgumentNullException.ThrowIfNull(synchronizationCapabilities);

        ViewportId = viewportId;
        Status = status;
        DeviceLuid = string.IsNullOrWhiteSpace(deviceLuid) ? null : deviceLuid.Trim();
        DeviceUuid = string.IsNullOrWhiteSpace(deviceUuid) ? null : deviceUuid.Trim();
        ImageHandleTypes = CleanValues(imageHandleTypes);
        SemaphoreHandleTypes = CleanValues(semaphoreHandleTypes);
        SynchronizationCapabilities = CleanValues(synchronizationCapabilities);
        Message = string.IsNullOrWhiteSpace(message) ? status.ToString() : message.Trim();
        CapturedAtUtc = capturedAtUtc;
    }

    public ViewportId ViewportId { get; }

    public ViewportCompositionStatus Status { get; }

    public string? DeviceLuid { get; }

    public string? DeviceUuid { get; }

    public IReadOnlyList<string> ImageHandleTypes { get; }

    public IReadOnlyList<string> SemaphoreHandleTypes { get; }

    public IReadOnlyList<string> SynchronizationCapabilities { get; }

    public string Message { get; }

    public DateTimeOffset CapturedAtUtc { get; }

    private static string[] CleanValues(IReadOnlyList<string> values)
    {
        return values
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Select(value => value.Trim())
            .ToArray();
    }
}
