using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedConfiguredModule
{
    internal ProjectCodePinnedConfiguredModule(
        ProjectCodePinnedModuleObject moduleObject,
        EditorModuleMetadata metadata,
        EditorModuleDeclaration declaration)
    {
        ArgumentNullException.ThrowIfNull(moduleObject);
        ArgumentNullException.ThrowIfNull(metadata);
        ArgumentNullException.ThrowIfNull(declaration);
        var entry = moduleObject.ModuleType.Entry;
        if (metadata.DefinitionId != entry.DefinitionId
            || !string.Equals(
                metadata.EntryTypeName,
                entry.TypeName,
                StringComparison.Ordinal)
            || metadata.Activation != entry.Activation
            || metadata.Handover != entry.Handover
            || declaration.DefinitionContext.DefinitionId
                != entry.DefinitionId)
        {
            throw new ArgumentException(
                "Configured module does not match its exact index entry.",
                nameof(declaration));
        }

        ModuleObject = moduleObject;
        Metadata = metadata;
        Declaration = declaration;
    }

    public ProjectCodePinnedModuleObject ModuleObject { get; }

    public EditorModuleMetadata Metadata { get; }

    public EditorModuleDeclaration Declaration { get; }
}

internal sealed class ProjectCodePinnedModuleConfiguration
{
    private const string Schema =
        "com.asharia.project-code-pinned-module-configuration-v1";

    internal ProjectCodePinnedModuleConfiguration(
        ProjectCodePinnedModuleConstruction construction,
        IReadOnlyList<ProjectCodePinnedConfiguredModule> modules)
    {
        ArgumentNullException.ThrowIfNull(construction);
        ArgumentNullException.ThrowIfNull(modules);
        var snapshot = modules.ToArray();
        if (snapshot.Length == 0
            || snapshot.Length != construction.Modules.Count
            || snapshot.Any(module => module is null))
        {
            throw new ArgumentException(
                "Configured modules do not match the exact construction.",
                nameof(modules));
        }

        for (var index = 0; index < snapshot.Length; ++index)
        {
            if (!ReferenceEquals(
                    snapshot[index].ModuleObject,
                    construction.Modules[index]))
            {
                throw new ArgumentException(
                    "Configured module order differs from the exact construction.",
                    nameof(modules));
            }
        }

        ConfigurationId = ComputeIdentity(construction);
        Construction = construction;
        Modules = Array.AsReadOnly(snapshot);
    }

    public string ConfigurationId { get; }

    public ProjectCodePinnedModuleConstruction Construction { get; }

    public IReadOnlyList<ProjectCodePinnedConfiguredModule> Modules
    {
        get;
    }

    private static string ComputeIdentity(
        ProjectCodePinnedModuleConstruction construction)
    {
        using var hash = IncrementalHash.CreateHash(
            HashAlgorithmName.SHA256);
        AppendString(hash, Schema);
        AppendString(hash, construction.ConstructionId);
        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset())
                .ToLowerInvariant();
    }

    private static void AppendString(
        IncrementalHash hash,
        string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        Span<byte> length = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32LittleEndian(length, bytes.Length);
        hash.AppendData(length);
        hash.AppendData(bytes);
    }
}

internal sealed record ProjectCodePinnedModuleConfigurationDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodePinnedModuleConfigurationResult
{
    private ProjectCodePinnedModuleConfigurationResult(
        ProjectCodePinnedModuleConfiguration? configuration,
        IReadOnlyList<ProjectCodePinnedModuleConfigurationDiagnostic>
            diagnostics)
    {
        Configuration = configuration;
        Diagnostics = diagnostics;
    }

    public ProjectCodePinnedModuleConfiguration? Configuration { get; }

    public IReadOnlyList<ProjectCodePinnedModuleConfigurationDiagnostic>
        Diagnostics
    {
        get;
    }

    public bool Succeeded =>
        Configuration is not null && Diagnostics.Count == 0;

    internal static ProjectCodePinnedModuleConfigurationResult Success(
        ProjectCodePinnedModuleConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        return new(configuration, []);
    }

    internal static ProjectCodePinnedModuleConfigurationResult Failure(
        IEnumerable<ProjectCodePinnedModuleConfigurationDiagnostic>
            diagnostics)
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
                "Failed module configuration requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}

internal sealed class ProjectCodePinnedModuleConfigurator
{
    private readonly object gate_ = new();
    private readonly Dictionary<Guid, Reservation> reservations_ = [];

    public ProjectCodePinnedModuleConfigurationResult Configure(
        ProjectCodePinnedModuleConstruction construction)
    {
        ArgumentNullException.ThrowIfNull(construction);
        var projectId = construction.ModuleTypes.Host.Image.Policy
            .Candidate.ModuleIndex.ProjectId;
        Reservation reservation;
        lock (gate_)
        {
            if (!reservations_.TryGetValue(projectId, out reservation!))
            {
                reservation = new Reservation(construction);
                reservations_.Add(projectId, reservation);
            }
        }

        return reservation.Configure(construction);
    }

    private static ProjectCodePinnedModuleConfigurationResult Failure(
        string code,
        string location,
        string message) =>
        ProjectCodePinnedModuleConfigurationResult.Failure(
            [new(code, location, message)]);

    private sealed class Reservation(
        ProjectCodePinnedModuleConstruction expectedConstruction)
    {
        private readonly object gate_ = new();
        private readonly List<ProjectCodePinnedConfiguredModule>
            retainedModules_ = [];
        private ProjectCodePinnedModuleConfigurationResult? result_;
        private bool isConfiguring_;

        public ProjectCodePinnedModuleConfigurationResult Configure(
            ProjectCodePinnedModuleConstruction construction)
        {
            lock (gate_)
            {
                if (!HasSameLineage(
                        expectedConstruction,
                        construction))
                {
                    return Failure(
                        "project-code.pinned-module-configuration.restart-required",
                        "project",
                        "A different pinned module construction requires a process restart.");
                }

                if (result_ is not null)
                {
                    return result_;
                }

                if (isConfiguring_)
                {
                    return Failure(
                        "project-code.pinned-module-configuration.in-progress",
                        "project",
                        "Pinned module configuration is already in progress.");
                }

                isConfiguring_ = true;
                var location = "module";
                try
                {
                    foreach (var moduleObject
                             in expectedConstruction.Modules)
                    {
                        var entry = moduleObject.ModuleType.Entry;
                        location = entry.TypeName;
                        var builder = new EditorModuleBuilder(
                            new EditorModuleDefinitionContext(
                                entry.DefinitionId));
                        moduleObject.Module.Configure(builder);
                        var declaration = builder.Build();
                        var metadata = new EditorModuleMetadata(
                            entry.DefinitionId,
                            entry.TypeName,
                            entry.Activation,
                            entry.Handover);
                        retainedModules_.Add(
                            new ProjectCodePinnedConfiguredModule(
                                moduleObject,
                                metadata,
                                declaration));
                    }

                    result_ =
                        ProjectCodePinnedModuleConfigurationResult.Success(
                            new ProjectCodePinnedModuleConfiguration(
                                expectedConstruction,
                                retainedModules_));
                }
                catch (Exception error) when (
                    error is not OutOfMemoryException)
                {
                    result_ = Failure(
                        "project-code.pinned-module-configuration.configure-failed-restart-required",
                        location,
                        "Pinned module configuration failed after execution began; restart is required.");
                }
                finally
                {
                    isConfiguring_ = false;
                }

                return result_;
            }
        }

        private static bool HasSameLineage(
            ProjectCodePinnedModuleConstruction expected,
            ProjectCodePinnedModuleConstruction candidate)
        {
            if (!string.Equals(
                    expected.ConstructionId,
                    candidate.ConstructionId,
                    StringComparison.Ordinal)
                || !ReferenceEquals(
                    expected.ModuleTypes.Host,
                    candidate.ModuleTypes.Host)
                || expected.Modules.Count != candidate.Modules.Count)
            {
                return false;
            }

            for (var index = 0; index < expected.Modules.Count; ++index)
            {
                var expectedModule = expected.Modules[index];
                var candidateModule = candidate.Modules[index];
                if (!ReferenceEquals(
                        expectedModule.ModuleType.Entry,
                        candidateModule.ModuleType.Entry)
                    || !ReferenceEquals(
                        expectedModule.ModuleType.Type,
                        candidateModule.ModuleType.Type)
                    || !ReferenceEquals(
                        expectedModule.Module,
                        candidateModule.Module))
                {
                    return false;
                }
            }

            return true;
        }
    }
}
