namespace Asharia.Studio.Distribution;

internal static class StudioDistributionPath
{
    private const string ExtendedPathPrefix = @"\\?\";
    private const string ExtendedUncPrefix = @"\\?\UNC\";
    private const string DevicePathPrefix = @"\\.\";
    private const string NativeDevicePathPrefix = @"\??\";

    public static string NormalizePublicAbsolutePath(string path)
    {
        var fullPath = Path.GetFullPath(path);
        if (!OperatingSystem.IsWindows())
        {
            return Path.TrimEndingDirectorySeparator(fullPath);
        }

        if (fullPath.StartsWith(ExtendedUncPrefix, StringComparison.OrdinalIgnoreCase))
        {
            fullPath = @"\\" + fullPath[ExtendedUncPrefix.Length..];
        }
        else if (fullPath.StartsWith(ExtendedPathPrefix, StringComparison.OrdinalIgnoreCase))
        {
            var candidate = fullPath[ExtendedPathPrefix.Length..];
            if (candidate.Length < 3
                || !char.IsAsciiLetter(candidate[0])
                || candidate[1] != Path.VolumeSeparatorChar
                || (candidate[2] != Path.DirectorySeparatorChar
                    && candidate[2] != Path.AltDirectorySeparatorChar))
            {
                throw new InvalidDataException(
                    "Only drive-letter and UNC extended paths can be represented by a public Distribution path.");
            }

            fullPath = candidate;
        }
        else if (fullPath.StartsWith(DevicePathPrefix, StringComparison.OrdinalIgnoreCase)
            || fullPath.StartsWith(NativeDevicePathPrefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException(
                "Device paths cannot be represented by a public Distribution path.");
        }

        fullPath = Path.GetFullPath(fullPath);
        if (fullPath.StartsWith(ExtendedPathPrefix, StringComparison.OrdinalIgnoreCase)
            || fullPath.StartsWith(DevicePathPrefix, StringComparison.OrdinalIgnoreCase)
            || fullPath.StartsWith(NativeDevicePathPrefix, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException(
                "The public Distribution path must not contain Windows device syntax.");
        }

        return Path.TrimEndingDirectorySeparator(fullPath);
    }
}
