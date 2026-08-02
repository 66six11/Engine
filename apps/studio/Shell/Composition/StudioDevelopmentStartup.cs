using System;

namespace Editor.Shell.Composition;

internal static class StudioDevelopmentStartup
{
    internal const string ReadOnlyObservationGrant =
        "--development-observation=readonly";

    internal static bool IsReadOnlyObservationGranted(
        ReadOnlySpan<string> arguments)
    {
        foreach (var argument in arguments)
        {
            if (string.Equals(
                    argument,
                    ReadOnlyObservationGrant,
                    StringComparison.Ordinal))
            {
                return true;
            }
        }

        return false;
    }
}
