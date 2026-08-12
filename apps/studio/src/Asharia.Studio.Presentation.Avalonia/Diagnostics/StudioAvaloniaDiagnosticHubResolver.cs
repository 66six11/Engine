using System;
using Asharia.Studio.Application.Diagnostics;
using Avalonia;

namespace Asharia.Studio.Presentation.Avalonia.Diagnostics;

internal static class StudioAvaloniaDiagnosticHubResolver
{
    public static IStudioDiagnosticHub RequireCurrent()
    {
        if (global::Avalonia.Application.Current is IStudioDiagnosticHubProvider provider)
        {
            return provider.Diagnostics;
        }

        throw new InvalidOperationException(
            "The current Avalonia application does not own a Studio diagnostic hub.");
    }
}
