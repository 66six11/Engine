using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Security;
using System.Security.Cryptography;
using System.Text;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedModuleObject
{
    internal ProjectCodePinnedModuleObject(
        ProjectCodePinnedModuleType moduleType,
        EditorModule module)
    {
        ArgumentNullException.ThrowIfNull(moduleType);
        ArgumentNullException.ThrowIfNull(module);
        if (!ReferenceEquals(module.GetType(), moduleType.Type))
        {
            throw new ArgumentException(
                "Constructed module object does not match its exact type receipt.",
                nameof(module));
        }

        ModuleType = moduleType;
        Module = module;
    }

    public ProjectCodePinnedModuleType ModuleType { get; }

    public EditorModule Module { get; }
}

internal sealed class ProjectCodePinnedModuleConstruction
{
    private const string Schema =
        "com.asharia.project-code-pinned-module-construction-v1";

    internal ProjectCodePinnedModuleConstruction(
        ProjectCodePinnedModuleTypeSet moduleTypes,
        IReadOnlyList<ProjectCodePinnedModuleObject> modules)
    {
        ArgumentNullException.ThrowIfNull(moduleTypes);
        ArgumentNullException.ThrowIfNull(modules);
        var snapshot = modules.ToArray();
        if (snapshot.Length == 0
            || snapshot.Length != moduleTypes.Modules.Count
            || snapshot.Any(module => module is null))
        {
            throw new ArgumentException(
                "Constructed modules do not match the exact type set.",
                nameof(modules));
        }

        for (var index = 0; index < snapshot.Length; ++index)
        {
            if (!ReferenceEquals(
                    snapshot[index].ModuleType,
                    moduleTypes.Modules[index]))
            {
                throw new ArgumentException(
                    "Constructed module order differs from the exact type set.",
                    nameof(modules));
            }
        }

        ConstructionId = ComputeIdentity(moduleTypes);
        ModuleTypes = moduleTypes;
        Modules = Array.AsReadOnly(snapshot);
    }

    public string ConstructionId { get; }

    public ProjectCodePinnedModuleTypeSet ModuleTypes { get; }

    public IReadOnlyList<ProjectCodePinnedModuleObject> Modules { get; }

    private static string ComputeIdentity(
        ProjectCodePinnedModuleTypeSet moduleTypes)
    {
        using var hash = IncrementalHash.CreateHash(
            HashAlgorithmName.SHA256);
        AppendString(hash, Schema);
        AppendString(hash, moduleTypes.ModuleTypeSetId);
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

internal sealed record ProjectCodePinnedModuleConstructionDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodePinnedModuleConstructionResult
{
    private ProjectCodePinnedModuleConstructionResult(
        ProjectCodePinnedModuleConstruction? construction,
        IReadOnlyList<ProjectCodePinnedModuleConstructionDiagnostic>
            diagnostics)
    {
        Construction = construction;
        Diagnostics = diagnostics;
    }

    public ProjectCodePinnedModuleConstruction? Construction { get; }

    public IReadOnlyList<ProjectCodePinnedModuleConstructionDiagnostic>
        Diagnostics
    {
        get;
    }

    public bool Succeeded =>
        Construction is not null && Diagnostics.Count == 0;

    internal static ProjectCodePinnedModuleConstructionResult Success(
        ProjectCodePinnedModuleConstruction construction)
    {
        ArgumentNullException.ThrowIfNull(construction);
        return new(construction, []);
    }

    internal static ProjectCodePinnedModuleConstructionResult Failure(
        IEnumerable<ProjectCodePinnedModuleConstructionDiagnostic>
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
                "Failed module construction requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}

internal sealed class ProjectCodePinnedModuleConstructor
{
    private readonly object gate_ = new();
    private readonly Dictionary<Guid, Reservation> reservations_ = [];

    public ProjectCodePinnedModuleConstructionResult Construct(
        ProjectCodePinnedModuleTypeSet moduleTypes)
    {
        ArgumentNullException.ThrowIfNull(moduleTypes);
        var projectId =
            moduleTypes.Host.Image.Policy.Candidate.ModuleIndex.ProjectId;
        Reservation reservation;
        lock (gate_)
        {
            if (!reservations_.TryGetValue(projectId, out reservation!))
            {
                reservation = new Reservation(moduleTypes);
                reservations_.Add(projectId, reservation);
            }
        }

        return reservation.Construct(moduleTypes);
    }

    private static ProjectCodePinnedModuleConstructionResult Failure(
        string code,
        string location,
        string message) =>
        ProjectCodePinnedModuleConstructionResult.Failure(
            [new(code, location, message)]);

    private sealed class Reservation(
        ProjectCodePinnedModuleTypeSet expectedModuleTypes)
    {
        private readonly object gate_ = new();
        private readonly List<ProjectCodePinnedModuleObject>
            retainedModules_ = [];
        private ProjectCodePinnedModuleConstructionResult? result_;
        private bool isConstructing_;

        public ProjectCodePinnedModuleConstructionResult Construct(
            ProjectCodePinnedModuleTypeSet moduleTypes)
        {
            lock (gate_)
            {
                if (!HasSameLineage(expectedModuleTypes, moduleTypes))
                {
                    return Failure(
                        "project-code.pinned-module-construction.restart-required",
                        "project",
                        "A different pinned module type set requires a process restart.");
                }

                if (result_ is not null)
                {
                    return result_;
                }

                if (isConstructing_)
                {
                    return Failure(
                        "project-code.pinned-module-construction.in-progress",
                        "project",
                        "Pinned module construction is already in progress.");
                }

                isConstructing_ = true;
                var location = "module";
                try
                {
                    foreach (var moduleType in expectedModuleTypes.Modules)
                    {
                        location = moduleType.Entry.TypeName;
                        var value = moduleType.Constructor.Invoke(null);
                        if (value is not EditorModule module
                            || !ReferenceEquals(
                                module.GetType(),
                                moduleType.Type))
                        {
                            throw new InvalidOperationException(
                                "Exact module constructor returned an unexpected object.");
                        }

                        retainedModules_.Add(
                            new ProjectCodePinnedModuleObject(
                                moduleType,
                                module));
                    }

                    result_ =
                        ProjectCodePinnedModuleConstructionResult.Success(
                            new ProjectCodePinnedModuleConstruction(
                                expectedModuleTypes,
                                retainedModules_));
                }
                catch (Exception error) when (
                    error is ArgumentException
                        or InvalidOperationException
                        or MemberAccessException
                        or NotSupportedException
                        or SecurityException
                        or TargetInvocationException
                        or TargetParameterCountException
                        or TypeInitializationException)
                {
                    result_ = Failure(
                        "project-code.pinned-module-construction.constructor-failed-restart-required",
                        location,
                        "Pinned module constructor failed after execution began; restart is required.");
                }
                finally
                {
                    isConstructing_ = false;
                }

                return result_;
            }
        }

        private static bool HasSameLineage(
            ProjectCodePinnedModuleTypeSet expected,
            ProjectCodePinnedModuleTypeSet candidate)
        {
            if (!string.Equals(
                    expected.ModuleTypeSetId,
                    candidate.ModuleTypeSetId,
                    StringComparison.Ordinal)
                || !ReferenceEquals(expected.Host, candidate.Host)
                || expected.Modules.Count != candidate.Modules.Count)
            {
                return false;
            }

            for (var index = 0; index < expected.Modules.Count; ++index)
            {
                var expectedModule = expected.Modules[index];
                var candidateModule = candidate.Modules[index];
                if (!ReferenceEquals(
                        expectedModule.Entry,
                        candidateModule.Entry)
                    || !ReferenceEquals(
                        expectedModule.Type,
                        candidateModule.Type))
                {
                    return false;
                }
            }

            return true;
        }
    }
}
