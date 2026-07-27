using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.RegularExpressions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedAssemblyHost
{
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);
    private readonly AssemblyLoadContext loadContext_;

    internal ProjectCodePinnedAssemblyHost(
        string hostId,
        ProjectCodePinnedLoadImageSnapshot image,
        AssemblyLoadContext loadContext,
        Assembly assembly)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(hostId);
        ArgumentNullException.ThrowIfNull(image);
        ArgumentNullException.ThrowIfNull(loadContext);
        ArgumentNullException.ThrowIfNull(assembly);
        if (!IdentityPattern.IsMatch(hostId))
        {
            throw new ArgumentException(
                "Pinned assembly host identity must be one canonical SHA-256 identity.",
                nameof(hostId));
        }

        var expected = image.Policy.Candidate.Publication.Report.Implementation;
        if (ReferenceEquals(loadContext, AssemblyLoadContext.Default)
            || loadContext.IsCollectible
            || !ReferenceEquals(
                AssemblyLoadContext.GetLoadContext(assembly),
                loadContext)
            || loadContext.Assemblies.Count() != 1
            || !ReferenceEquals(loadContext.Assemblies.Single(), assembly)
            || !string.IsNullOrEmpty(assembly.Location)
            || !HasBindingIdentity(assembly.GetName(), expected.Identity)
            || assembly.ManifestModule.ModuleVersionId != expected.Mvid)
        {
            throw new ArgumentException(
                "Pinned assembly host does not match the exact load image.",
                nameof(assembly));
        }

        HostId = hostId;
        Image = image;
        Assembly = assembly;
        loadContext_ = loadContext;
    }

    public string HostId { get; }

    public ProjectCodePinnedLoadImageSnapshot Image { get; }

    public Assembly Assembly { get; }

    public string LoadContextName => loadContext_.Name ?? "";

    public bool IsCollectible => loadContext_.IsCollectible;

    public int AssemblyCount => loadContext_.Assemblies.Count();

    internal static bool HasBindingIdentity(
        AssemblyName actual,
        ProjectCodeAssemblyIdentity expected)
    {
        ArgumentNullException.ThrowIfNull(actual);
        ArgumentNullException.ThrowIfNull(expected);
        var culture = string.IsNullOrWhiteSpace(actual.CultureName)
            ? "neutral"
            : actual.CultureName;
        var token = actual.GetPublicKeyToken();
        var publicKeyToken = token is null || token.Length == 0
            ? "null"
            : Convert.ToHexString(token).ToLowerInvariant();
        return string.Equals(
                actual.Name,
                expected.SimpleName,
                StringComparison.OrdinalIgnoreCase)
            && actual.Version == expected.Version
            && string.Equals(
                culture,
                expected.Culture,
                StringComparison.OrdinalIgnoreCase)
            && string.Equals(
                publicKeyToken,
                expected.PublicKeyToken,
                StringComparison.OrdinalIgnoreCase);
    }
}

internal sealed record ProjectCodePinnedAssemblyLoadDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodePinnedAssemblyLoadResult
{
    private ProjectCodePinnedAssemblyLoadResult(
        ProjectCodePinnedAssemblyHost? host,
        IReadOnlyList<ProjectCodePinnedAssemblyLoadDiagnostic> diagnostics)
    {
        Host = host;
        Diagnostics = diagnostics;
    }

    public ProjectCodePinnedAssemblyHost? Host { get; }

    public IReadOnlyList<ProjectCodePinnedAssemblyLoadDiagnostic> Diagnostics
    {
        get;
    }

    public bool Succeeded => Host is not null && Diagnostics.Count == 0;

    internal static ProjectCodePinnedAssemblyLoadResult Success(
        ProjectCodePinnedAssemblyHost host)
    {
        ArgumentNullException.ThrowIfNull(host);
        return new(host, []);
    }

    internal static ProjectCodePinnedAssemblyLoadResult Failure(
        IEnumerable<ProjectCodePinnedAssemblyLoadDiagnostic> diagnostics)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        var snapshot = diagnostics
            .Distinct()
            .OrderBy(item => item.Location, StringComparer.Ordinal)
            .ThenBy(item => item.Code, StringComparer.Ordinal)
            .ThenBy(item => item.Message, StringComparer.Ordinal)
            .ToArray();
        if (snapshot.Length == 0)
        {
            throw new ArgumentException(
                "Failed pinned assembly load requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}
