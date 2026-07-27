using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed record ProjectCodeModuleIndexEntry
{
    public ProjectCodeModuleIndexEntry(
        EditorModuleDefinitionId definitionId,
        string typeName,
        EditorModuleActivationPolicy activation,
        EditorModuleHandoverPolicy handover)
    {
        if (!definitionId.IsValid)
        {
            throw new ArgumentException(
                "Module index entry requires one valid definition identity.",
                nameof(definitionId));
        }

        ArgumentException.ThrowIfNullOrWhiteSpace(typeName);
        if (!typeName.IsNormalized()
            || typeName.Any(char.IsControl)
            || typeName.Contains('+')
            || typeName.Contains('`')
            || typeName.IndexOfAny(
                ['/', '\\', ':', ',', '[', ']']) >= 0)
        {
            throw new ArgumentException(
                "Module index entry requires one canonical top-level CLR type name.",
                nameof(typeName));
        }

        if (!Enum.IsDefined(activation))
        {
            throw new ArgumentOutOfRangeException(nameof(activation));
        }

        if (!Enum.IsDefined(handover))
        {
            throw new ArgumentOutOfRangeException(nameof(handover));
        }

        DefinitionId = definitionId;
        TypeName = typeName;
        Activation = activation;
        Handover = handover;
    }

    public EditorModuleDefinitionId DefinitionId { get; }

    public string TypeName { get; }

    public EditorModuleActivationPolicy Activation { get; }

    public EditorModuleHandoverPolicy Handover { get; }
}

internal sealed record ProjectCodeModuleIndexReport
{
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    public ProjectCodeModuleIndexReport(
        string indexId,
        string publicationId,
        Guid projectId,
        string assemblyName,
        IReadOnlyList<ProjectCodeModuleIndexEntry> entries)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(indexId);
        ArgumentException.ThrowIfNullOrWhiteSpace(publicationId);
        ArgumentException.ThrowIfNullOrWhiteSpace(assemblyName);
        ArgumentNullException.ThrowIfNull(entries);
        if (!IdentityPattern.IsMatch(indexId)
            || !IdentityPattern.IsMatch(publicationId))
        {
            throw new ArgumentException(
                "Module index identities must be canonical SHA-256 identities.");
        }

        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "Module index requires one non-empty project id.",
                nameof(projectId));
        }

        var snapshot = entries.ToArray();
        if (snapshot.Any(entry => entry is null)
            || snapshot
                .GroupBy(
                    entry => entry.DefinitionId,
                    EqualityComparer<EditorModuleDefinitionId>.Default)
                .Any(group => group.Count() != 1)
            || snapshot
                .GroupBy(entry => entry.TypeName, StringComparer.Ordinal)
                .Any(group => group.Count() != 1))
        {
            throw new ArgumentException(
                "Module index entries must have unique definitions and CLR type names.",
                nameof(entries));
        }

        IndexId = indexId;
        PublicationId = publicationId;
        ProjectId = projectId;
        AssemblyName = assemblyName;
        Entries = Array.AsReadOnly(snapshot);
    }

    public string IndexId { get; }

    public string PublicationId { get; }

    public Guid ProjectId { get; }

    public string AssemblyName { get; }

    public IReadOnlyList<ProjectCodeModuleIndexEntry> Entries { get; }
}

internal sealed record ProjectCodeModuleIndexDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodeModuleIndexResult
{
    private ProjectCodeModuleIndexResult(
        ProjectCodeModuleIndexReport? report,
        IReadOnlyList<ProjectCodeModuleIndexDiagnostic> diagnostics)
    {
        Report = report;
        Diagnostics = diagnostics;
    }

    public ProjectCodeModuleIndexReport? Report { get; }

    public IReadOnlyList<ProjectCodeModuleIndexDiagnostic> Diagnostics { get; }

    public bool Succeeded => Report is not null && Diagnostics.Count == 0;

    internal static ProjectCodeModuleIndexResult Success(
        ProjectCodeModuleIndexReport report)
    {
        ArgumentNullException.ThrowIfNull(report);
        return new(report, []);
    }

    internal static ProjectCodeModuleIndexResult Failure(
        IEnumerable<ProjectCodeModuleIndexDiagnostic> diagnostics)
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
                "Failed module indexing requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}
