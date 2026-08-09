using System;
using System.Collections.Generic;
using System.Runtime.ExceptionServices;

namespace Asharia.Studio.Presentation.Avalonia.Viewports;

internal readonly record struct ViewportPresentationVisualMutationStep(
    Action Apply,
    Action Restore);

internal sealed class ViewportPresentationVisualMutationAmbiguousException : Exception
{
    public ViewportPresentationVisualMutationAmbiguousException(
        Exception publicationFailure,
        IReadOnlyList<Exception> restorationFailures)
        : base(
            "A viewport visual mutation failed and its previous state could not be restored.",
            new AggregateException(
                "Viewport visual publication and restoration both failed.",
                Combine(publicationFailure, restorationFailures)))
    {
        PublicationFailure = publicationFailure;
        RestorationFailures = restorationFailures;
    }

    public Exception PublicationFailure { get; }

    public IReadOnlyList<Exception> RestorationFailures { get; }

    private static IEnumerable<Exception> Combine(
        Exception publicationFailure,
        IReadOnlyList<Exception> restorationFailures)
    {
        yield return publicationFailure;
        foreach (var failure in restorationFailures)
        {
            yield return failure;
        }
    }
}

/// <summary>
/// Applies a visible-property set as one logical mutation. If any setter or test seam fails, every
/// property is restored in reverse order. A failed restore is explicitly ambiguous; callers must
/// retain both possible fronts instead of guessing which one the compositor observed.
/// </summary>
internal static class ViewportPresentationVisualMutation
{
    public static void ApplyStrong(
        IReadOnlyList<ViewportPresentationVisualMutationStep> steps)
    {
        ArgumentNullException.ThrowIfNull(steps);
        try
        {
            foreach (var step in steps)
            {
                ArgumentNullException.ThrowIfNull(step.Apply);
                ArgumentNullException.ThrowIfNull(step.Restore);
                step.Apply();
            }
        }
        catch (Exception publicationFailure)
        {
            var restorationFailures = RestoreAll(steps);
            if (restorationFailures.Count != 0)
            {
                throw new ViewportPresentationVisualMutationAmbiguousException(
                    publicationFailure,
                    restorationFailures);
            }

            ExceptionDispatchInfo.Capture(publicationFailure).Throw();
            throw;
        }
    }

    public static void RestoreStrong(
        IReadOnlyList<ViewportPresentationVisualMutationStep> steps)
    {
        ArgumentNullException.ThrowIfNull(steps);
        var restorationFailures = RestoreAll(steps);
        if (restorationFailures.Count != 0)
        {
            throw new ViewportPresentationVisualMutationAmbiguousException(
                new InvalidOperationException(
                    "The viewport visual rollback could not restore its previous state."),
                restorationFailures);
        }
    }

    private static List<Exception> RestoreAll(
        IReadOnlyList<ViewportPresentationVisualMutationStep> steps)
    {
        var failures = new List<Exception>();
        for (var index = steps.Count - 1; index >= 0; index--)
        {
            try
            {
                ArgumentNullException.ThrowIfNull(steps[index].Restore);
                steps[index].Restore();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
        return failures;
    }
}
