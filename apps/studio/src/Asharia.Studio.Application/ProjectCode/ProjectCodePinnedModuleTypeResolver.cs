using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal sealed class ProjectCodePinnedModuleType
{
    internal ProjectCodePinnedModuleType(
        ProjectCodePinnedAssemblyHost host,
        ProjectCodeModuleIndexEntry entry,
        Type type)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(entry);
        ArgumentNullException.ThrowIfNull(type);
        var constructor = type.GetConstructor(Type.EmptyTypes);
        if (!ReferenceEquals(type.Assembly, host.Assembly)
            || !string.Equals(
                type.FullName,
                entry.TypeName,
                StringComparison.Ordinal)
            || !type.IsClass
            || type.IsNested
            || !type.IsPublic
            || type.IsAbstract
            || !type.IsSealed
            || type.IsGenericType
            || type.ContainsGenericParameters
            || !ReferenceEquals(type.BaseType, typeof(EditorModule))
            || constructor is null
            || !constructor.IsPublic
            || constructor.IsStatic
            || constructor.GetParameters().Length != 0
            || !ReferenceEquals(constructor.DeclaringType, type))
        {
            throw new ArgumentException(
                "Resolved module type does not match its exact index entry.",
                nameof(type));
        }

        Entry = entry;
        Type = type;
        Constructor = constructor;
    }

    public ProjectCodeModuleIndexEntry Entry { get; }

    public Type Type { get; }

    public ConstructorInfo Constructor { get; }
}

internal sealed class ProjectCodePinnedModuleTypeSet
{
    private const string Schema =
        "com.asharia.project-code-pinned-module-type-set-v1";
    private static readonly Regex IdentityPattern = new(
        "^sha256-[0-9a-f]{64}$",
        RegexOptions.CultureInvariant);

    internal ProjectCodePinnedModuleTypeSet(
        string moduleTypeSetId,
        ProjectCodePinnedAssemblyHost host,
        IReadOnlyList<ProjectCodePinnedModuleType> modules)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(moduleTypeSetId);
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(modules);
        var snapshot = modules.ToArray();
        var expected =
            host.Image.Policy.Candidate.ModuleIndex.Entries;
        if (!IdentityPattern.IsMatch(moduleTypeSetId)
            || !string.Equals(
                moduleTypeSetId,
                ComputeIdentity(host),
                StringComparison.Ordinal)
            || snapshot.Length == 0
            || snapshot.Length != expected.Count
            || snapshot.Any(module => module is null))
        {
            throw new ArgumentException(
                "Pinned module type set does not match its exact host.",
                nameof(modules));
        }

        for (var index = 0; index < snapshot.Length; ++index)
        {
            if (!ReferenceEquals(snapshot[index].Entry, expected[index])
                || !ReferenceEquals(
                    snapshot[index].Type.Assembly,
                    host.Assembly))
            {
                throw new ArgumentException(
                    "Pinned module type order differs from its exact index.",
                    nameof(modules));
            }
        }

        ModuleTypeSetId = moduleTypeSetId;
        Host = host;
        Modules = Array.AsReadOnly(snapshot);
    }

    public string ModuleTypeSetId { get; }

    public ProjectCodePinnedAssemblyHost Host { get; }

    public IReadOnlyList<ProjectCodePinnedModuleType> Modules { get; }

    internal static string ComputeIdentity(
        ProjectCodePinnedAssemblyHost host)
    {
        ArgumentNullException.ThrowIfNull(host);
        using var hash = IncrementalHash.CreateHash(
            HashAlgorithmName.SHA256);
        AppendString(hash, Schema);
        AppendString(hash, host.HostId);
        AppendString(
            hash,
            host.Image.Policy.Candidate.ModuleIndex.IndexId);
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

internal sealed record ProjectCodePinnedModuleTypeDiagnostic(
    string Code,
    string Location,
    string Message);

internal sealed class ProjectCodePinnedModuleTypeResolutionResult
{
    private ProjectCodePinnedModuleTypeResolutionResult(
        ProjectCodePinnedModuleTypeSet? moduleTypes,
        IReadOnlyList<ProjectCodePinnedModuleTypeDiagnostic> diagnostics)
    {
        ModuleTypes = moduleTypes;
        Diagnostics = diagnostics;
    }

    public ProjectCodePinnedModuleTypeSet? ModuleTypes { get; }

    public IReadOnlyList<ProjectCodePinnedModuleTypeDiagnostic> Diagnostics
    {
        get;
    }

    public bool Succeeded =>
        ModuleTypes is not null && Diagnostics.Count == 0;

    internal static ProjectCodePinnedModuleTypeResolutionResult Success(
        ProjectCodePinnedModuleTypeSet moduleTypes)
    {
        ArgumentNullException.ThrowIfNull(moduleTypes);
        return new(moduleTypes, []);
    }

    internal static ProjectCodePinnedModuleTypeResolutionResult Failure(
        IEnumerable<ProjectCodePinnedModuleTypeDiagnostic> diagnostics)
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
                "Failed module type resolution requires diagnostics.",
                nameof(diagnostics));
        }

        return new(null, Array.AsReadOnly(snapshot));
    }
}

internal static class ProjectCodePinnedModuleTypeResolver
{
    public static ProjectCodePinnedModuleTypeResolutionResult Resolve(
        ProjectCodePinnedAssemblyHost host)
    {
        ArgumentNullException.ThrowIfNull(host);
        var modules = new List<ProjectCodePinnedModuleType>();
        foreach (var entry in
                 host.Image.Policy.Candidate.ModuleIndex.Entries)
        {
            Type? type;
            try
            {
                type = host.Assembly.GetType(
                    entry.TypeName,
                    throwOnError: false,
                    ignoreCase: false);
            }
            catch (Exception error) when (
                error is ArgumentException
                    or BadImageFormatException
                    or IOException
                    or NotSupportedException
                    or TypeLoadException)
            {
                return Failure(
                    "project-code.pinned-module-types.resolution-failed",
                    entry.TypeName,
                    "Pinned module type could not be resolved safely.");
            }

            if (type is null)
            {
                return Failure(
                    "project-code.pinned-module-types.type-missing",
                    entry.TypeName,
                    "Pinned module type is missing from the exact root assembly.");
            }

            try
            {
                modules.Add(new ProjectCodePinnedModuleType(
                    host,
                    entry,
                    type));
            }
            catch (Exception error) when (
                error is ArgumentException
                    or BadImageFormatException
                    or IOException
                    or NotSupportedException
                    or TypeLoadException)
            {
                return Failure(
                    "project-code.pinned-module-types.runtime-shape-mismatch",
                    entry.TypeName,
                    "Pinned module runtime type does not match its exact index.");
            }
        }

        return ProjectCodePinnedModuleTypeResolutionResult.Success(
            new ProjectCodePinnedModuleTypeSet(
                ProjectCodePinnedModuleTypeSet.ComputeIdentity(host),
                host,
                modules));
    }

    private static ProjectCodePinnedModuleTypeResolutionResult Failure(
        string code,
        string location,
        string message) =>
        ProjectCodePinnedModuleTypeResolutionResult.Failure(
            [new(code, location, message)]);
}
