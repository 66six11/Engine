using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Extensions;

namespace Asharia.Studio.Application.ProjectCode;

internal static class ProjectCodeModuleIndexer
{
    private const string AttributeNamespace =
        "Asharia.Editor.Extensions";
    private const string AttributeName = "EditorModuleAttribute";
    private const string ModuleBaseName = "EditorModule";
    private static readonly byte[] AttributeConstructorSignature =
        [0x20, 0x01, 0x01, 0x0e];
    private static readonly byte[] DefaultConstructorSignature =
        [0x20, 0x00, 0x01];

    public static async Task<ProjectCodeModuleIndexResult> IndexAsync(
        ProjectCodeArtifactPublicationReceipt publication,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(publication);
        cancellationToken.ThrowIfCancellationRequested();
        if (!await ProjectCodeArtifactPublisher
                .IsPublicationCurrentAsync(publication, cancellationToken)
                .ConfigureAwait(false))
        {
            return Failure(
                "project-code.module-index.publication-not-current",
                "publication",
                "Module indexing requires one current closed artifact publication.");
        }

        var diagnostics = new List<ProjectCodeModuleIndexDiagnostic>();
        var editorIdentity = publication.Report.EditorContractIdentity;
        var assemblyId = CreateAssemblyId(publication, diagnostics);
        if (assemblyId is null)
        {
            return ProjectCodeModuleIndexResult.Failure(diagnostics);
        }

        var implementation = InspectAssembly(
            CombinePortable(
                publication.AbsoluteRoot,
                publication.Implementation.RelativePath),
            assemblyId.Value,
            editorIdentity,
            diagnostics,
            "implementation",
            cancellationToken);
        var reference = InspectAssembly(
            CombinePortable(
                publication.AbsoluteRoot,
                publication.ReferenceAssembly.RelativePath),
            assemblyId.Value,
            editorIdentity,
            diagnostics,
            "reference-assembly",
            cancellationToken);
        if (!await ProjectCodeArtifactPublisher
                .IsPublicationCurrentAsync(publication, cancellationToken)
                .ConfigureAwait(false))
        {
            return Failure(
                "project-code.module-index.publication-changed",
                "publication",
                "Artifact publication changed while module metadata was indexed.");
        }

        if (diagnostics.Count != 0
            || implementation is null
            || reference is null)
        {
            return ProjectCodeModuleIndexResult.Failure(diagnostics);
        }

        if (!implementation
                .Select(EntryIdentity)
                .SequenceEqual(
                    reference.Select(EntryIdentity),
                    StringComparer.Ordinal))
        {
            return Failure(
                "project-code.module-index.assembly-surface-mismatch",
                "reference-assembly",
                "Implementation and reference assembly module surfaces differ.");
        }

        var indexId = ComputeIndexId(publication, implementation);
        return ProjectCodeModuleIndexResult.Success(
            new ProjectCodeModuleIndexReport(
                indexId,
                publication.PublicationId,
                publication.Report.ProjectId,
                publication.Report.AssemblyName,
                Array.AsReadOnly(implementation)));
    }

    private static EditorAssemblyId? CreateAssemblyId(
        ProjectCodeArtifactPublicationReceipt publication,
        ICollection<ProjectCodeModuleIndexDiagnostic> diagnostics)
    {
        var projectId = publication.Report.ProjectId
            .ToString("D")
            .ToLowerInvariant();
        if (!PackageName.TryCreate(
                $"project:{projectId}:editor",
                out var package)
            || !EditorAssemblyName.TryCreate(
                publication.Report.AssemblyName,
                out var assembly))
        {
            diagnostics.Add(Diagnostic(
                "project-code.module-index.owner-invalid",
                "publication",
                "Publication identities cannot form one canonical project Editor assembly owner."));
            return null;
        }

        return EditorAssemblyId.Create(package, assembly);
    }

    private static ProjectCodeModuleIndexEntry[]? InspectAssembly(
        string path,
        EditorAssemblyId assemblyId,
        ProjectCodeAssemblyIdentity editorIdentity,
        ICollection<ProjectCodeModuleIndexDiagnostic> diagnostics,
        string location,
        CancellationToken cancellationToken)
    {
        try
        {
            using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                1024 * 1024,
                FileOptions.SequentialScan);
            using var peReader = new PEReader(
                stream,
                PEStreamOptions.LeaveOpen);
            if (!peReader.HasMetadata
                || peReader.PEHeaders.CorHeader is null)
            {
                throw new BadImageFormatException();
            }

            var reader = peReader.GetMetadataReader(
                MetadataReaderOptions.None);
            var entries = new List<ProjectCodeModuleIndexEntry>();
            foreach (var handle in reader.TypeDefinitions)
            {
                cancellationToken.ThrowIfCancellationRequested();
                InspectType(
                    reader,
                    reader.GetTypeDefinition(handle),
                    assemblyId,
                    editorIdentity,
                    entries,
                    diagnostics,
                    location);
            }

            var ordered = entries
                .OrderBy(EntryIdentity, StringComparer.Ordinal)
                .ToArray();
            if (ordered
                    .GroupBy(
                        entry => entry.DefinitionId,
                        EqualityComparer<EditorModuleDefinitionId>.Default)
                    .Any(group => group.Count() != 1)
                || ordered
                    .GroupBy(
                        entry => entry.TypeName,
                        StringComparer.Ordinal)
                    .Any(group => group.Count() != 1))
            {
                diagnostics.Add(Diagnostic(
                    "project-code.module-index.entry-duplicate",
                    location,
                    "Module definitions and CLR type names must be unique."));
            }

            return diagnostics.Any(diagnostic =>
                diagnostic.Location.StartsWith(
                    location,
                    StringComparison.Ordinal))
                ? null
                : ordered;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception error) when (
            error is ArgumentException
                or BadImageFormatException
                or IOException
                or InvalidDataException
                or NotSupportedException
                or UnauthorizedAccessException)
        {
            diagnostics.Add(Diagnostic(
                "project-code.module-index.assembly-invalid",
                location,
                "Published module assembly metadata is invalid or unreadable."));
            return null;
        }
    }

    private static void InspectType(
        MetadataReader reader,
        TypeDefinition type,
        EditorAssemblyId assemblyId,
        ProjectCodeAssemblyIdentity editorIdentity,
        ICollection<ProjectCodeModuleIndexEntry> entries,
        ICollection<ProjectCodeModuleIndexDiagnostic> diagnostics,
        string location)
    {
        var typeName = ReadTypeName(reader, type);
        var typeLocation = location;
        var attributes = new List<CustomAttribute>();
        foreach (var handle in type.GetCustomAttributes())
        {
            var attribute = reader.GetCustomAttribute(handle);
            switch (MatchModuleAttribute(
                reader,
                attribute,
                editorIdentity))
            {
                case AttributeMatch.Exact:
                    attributes.Add(attribute);
                    break;
                case AttributeMatch.Spoofed:
                    diagnostics.Add(Diagnostic(
                        "project-code.module-index.attribute-scope-invalid",
                        typeLocation,
                        "EditorModuleAttribute must come from the exact Asharia.Editor contract."));
                    break;
            }
        }

        var isDirectModule = IsExactTypeReference(
            reader,
            type.BaseType,
            AttributeNamespace,
            ModuleBaseName,
            editorIdentity);
        var isAbstract = (type.Attributes & TypeAttributes.Abstract) != 0;
        if (attributes.Count == 0)
        {
            if (isDirectModule && !isAbstract)
            {
                diagnostics.Add(Diagnostic(
                    "project-code.module-index.attribute-missing",
                    typeLocation,
                    "Concrete direct EditorModule subtype requires one EditorModuleAttribute."));
            }

            return;
        }

        if (attributes.Count != 1)
        {
            diagnostics.Add(Diagnostic(
                "project-code.module-index.attribute-duplicate",
                typeLocation,
                "Editor module type requires exactly one EditorModuleAttribute."));
            return;
        }

        if (!HasSupportedTypeShape(
                reader,
                type,
                isDirectModule,
                isAbstract))
        {
            diagnostics.Add(Diagnostic(
                "project-code.module-index.type-shape-invalid",
                typeLocation,
                "Indexed module must be a public top-level sealed non-generic direct EditorModule subtype with a public parameterless constructor."));
            return;
        }

        var declaration = DecodeModuleAttribute(
            reader,
            attributes[0],
            editorIdentity);
        if (declaration is null)
        {
            diagnostics.Add(Diagnostic(
                "project-code.module-index.attribute-invalid",
                typeLocation,
                "EditorModuleAttribute constructor or named values are invalid."));
            return;
        }

        var definitionId = EditorModuleDefinitionId.Create(
            assemblyId,
            declaration.Value.ModuleId,
            declaration.Value.Scope);
        entries.Add(new ProjectCodeModuleIndexEntry(
            definitionId,
            typeName,
            declaration.Value.Activation,
            declaration.Value.Handover));
    }

    private static bool HasSupportedTypeShape(
        MetadataReader reader,
        TypeDefinition type,
        bool isDirectModule,
        bool isAbstract)
    {
        var visibility = type.Attributes & TypeAttributes.VisibilityMask;
        return isDirectModule
            && visibility == TypeAttributes.Public
            && !isAbstract
            && (type.Attributes & TypeAttributes.Sealed) != 0
            && type.GetGenericParameters().Count == 0
            && HasPublicParameterlessConstructor(reader, type);
    }

    private static bool HasPublicParameterlessConstructor(
        MetadataReader reader,
        TypeDefinition type)
    {
        foreach (var handle in type.GetMethods())
        {
            var method = reader.GetMethodDefinition(handle);
            if (string.Equals(
                    reader.GetString(method.Name),
                    ".ctor",
                    StringComparison.Ordinal)
                && (method.Attributes & MethodAttributes.MemberAccessMask)
                    == MethodAttributes.Public
                && (method.Attributes & MethodAttributes.Static) == 0
                && reader.GetBlobBytes(method.Signature)
                    .SequenceEqual(DefaultConstructorSignature))
            {
                return true;
            }
        }

        return false;
    }

    private static AttributeMatch MatchModuleAttribute(
        MetadataReader reader,
        CustomAttribute attribute,
        ProjectCodeAssemblyIdentity editorIdentity)
    {
        if (attribute.Constructor.Kind != HandleKind.MemberReference)
        {
            return IsLocalModuleAttribute(reader, attribute)
                ? AttributeMatch.Spoofed
                : AttributeMatch.None;
        }

        var member = reader.GetMemberReference(
            (MemberReferenceHandle)attribute.Constructor);
        if (member.Parent.Kind != HandleKind.TypeReference)
        {
            return AttributeMatch.None;
        }

        var type = reader.GetTypeReference(
            (TypeReferenceHandle)member.Parent);
        if (!string.Equals(
                reader.GetString(type.Namespace),
                AttributeNamespace,
                StringComparison.Ordinal)
            || !string.Equals(
                reader.GetString(type.Name),
                AttributeName,
                StringComparison.Ordinal))
        {
            return AttributeMatch.None;
        }

        return IsExactTypeReference(
                reader,
                (TypeReferenceHandle)member.Parent,
                AttributeNamespace,
                AttributeName,
                editorIdentity)
            && string.Equals(
                reader.GetString(member.Name),
                ".ctor",
                StringComparison.Ordinal)
            && reader.GetBlobBytes(member.Signature)
                .SequenceEqual(AttributeConstructorSignature)
                ? AttributeMatch.Exact
                : AttributeMatch.Spoofed;
    }

    private static bool IsLocalModuleAttribute(
        MetadataReader reader,
        CustomAttribute attribute)
    {
        if (attribute.Constructor.Kind != HandleKind.MethodDefinition)
        {
            return false;
        }

        var method = reader.GetMethodDefinition(
            (MethodDefinitionHandle)attribute.Constructor);
        var type = reader.GetTypeDefinition(method.GetDeclaringType());
        return string.Equals(
                reader.GetString(type.Namespace),
                AttributeNamespace,
                StringComparison.Ordinal)
            && string.Equals(
                reader.GetString(type.Name),
                AttributeName,
                StringComparison.Ordinal);
    }

    private static ModuleDeclaration? DecodeModuleAttribute(
        MetadataReader reader,
        CustomAttribute attribute,
        ProjectCodeAssemblyIdentity editorIdentity)
    {
        try
        {
            var provider = new AttributeTypeProvider(editorIdentity);
            var value = attribute.DecodeValue(provider);
            if (value.FixedArguments.Length != 1
                || value.FixedArguments[0].Type
                    != AttributeValueType.String
                || value.FixedArguments[0].Value is not string id
                || !ModuleLocalId.TryCreate(id, out var moduleId))
            {
                return null;
            }

            var scope = EditorModuleScopeKind.Project;
            var activation = EditorModuleActivationPolicy.OnScopeReady;
            var handover = EditorModuleHandoverPolicy.Coexist;
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var argument in value.NamedArguments)
            {
                if (argument.Kind
                        != CustomAttributeNamedArgumentKind.Property
                    || string.IsNullOrEmpty(argument.Name)
                    || !seen.Add(argument.Name)
                    || argument.Value is not int raw)
                {
                    return null;
                }

                switch (argument.Name)
                {
                    case nameof(EditorModuleAttribute.Scope)
                        when argument.Type
                            == AttributeValueType.Scope:
                        scope = (EditorModuleScopeKind)raw;
                        if (!Enum.IsDefined(scope))
                        {
                            return null;
                        }

                        break;
                    case nameof(EditorModuleAttribute.Activation)
                        when argument.Type
                            == AttributeValueType.Activation:
                        activation = (EditorModuleActivationPolicy)raw;
                        if (!Enum.IsDefined(activation))
                        {
                            return null;
                        }

                        break;
                    case nameof(EditorModuleAttribute.Handover)
                        when argument.Type
                            == AttributeValueType.Handover:
                        handover = (EditorModuleHandoverPolicy)raw;
                        if (!Enum.IsDefined(handover))
                        {
                            return null;
                        }

                        break;
                    default:
                        return null;
                }
            }

            return new(moduleId, scope, activation, handover);
        }
        catch (Exception error) when (
            error is BadImageFormatException
                or InvalidOperationException)
        {
            return null;
        }
    }

    private static bool IsExactTypeReference(
        MetadataReader reader,
        EntityHandle handle,
        string expectedNamespace,
        string expectedName,
        ProjectCodeAssemblyIdentity expectedAssembly)
    {
        if (handle.Kind != HandleKind.TypeReference)
        {
            return false;
        }

        return IsExactTypeReference(
            reader,
            (TypeReferenceHandle)handle,
            expectedNamespace,
            expectedName,
            expectedAssembly);
    }

    private static bool IsExactTypeReference(
        MetadataReader reader,
        TypeReferenceHandle handle,
        string expectedNamespace,
        string expectedName,
        ProjectCodeAssemblyIdentity expectedAssembly)
    {
        var type = reader.GetTypeReference(handle);
        if (!string.Equals(
                reader.GetString(type.Namespace),
                expectedNamespace,
                StringComparison.Ordinal)
            || !string.Equals(
                reader.GetString(type.Name),
                expectedName,
                StringComparison.Ordinal)
            || type.ResolutionScope.Kind
                != HandleKind.AssemblyReference)
        {
            return false;
        }

        return HasExactIdentity(
            ReadReferenceIdentity(
                reader,
                reader.GetAssemblyReference(
                    (AssemblyReferenceHandle)type.ResolutionScope)),
            expectedAssembly);
    }

    private static ProjectCodeAssemblyIdentity ReadReferenceIdentity(
        MetadataReader reader,
        AssemblyReference reference)
    {
        var keyOrToken = reader.GetBlobBytes(
            reference.PublicKeyOrToken);
        var token = "null";
        if (keyOrToken.Length != 0)
        {
            token = (reference.Flags & AssemblyFlags.PublicKey) != 0
                ? ComputePublicKeyToken(keyOrToken)
                : Convert.ToHexString(keyOrToken).ToLowerInvariant();
        }

        var culture = reference.Culture.IsNil
            ? "neutral"
            : reader.GetString(reference.Culture);
        if (string.IsNullOrEmpty(culture))
        {
            culture = "neutral";
        }

        return new ProjectCodeAssemblyIdentity(
            reader.GetString(reference.Name),
            reference.Version,
            culture,
            token);
    }

    private static string ComputePublicKeyToken(byte[] publicKey)
    {
#pragma warning disable CA5350 // CLR public-key tokens are defined using SHA-1.
        var hash = SHA1.HashData(publicKey);
#pragma warning restore CA5350
        var token = hash[^8..];
        Array.Reverse(token);
        return Convert.ToHexString(token).ToLowerInvariant();
    }

    private static bool HasExactIdentity(
        ProjectCodeAssemblyIdentity left,
        ProjectCodeAssemblyIdentity right) =>
        string.Equals(left.FullName, right.FullName, StringComparison.Ordinal);

    private static string ReadTypeName(
        MetadataReader reader,
        TypeDefinition type)
    {
        var name = reader.GetString(type.Name);
        var typeNamespace = reader.GetString(type.Namespace);
        return string.IsNullOrEmpty(typeNamespace)
            ? name
            : typeNamespace + "." + name;
    }

    private static string ComputeIndexId(
        ProjectCodeArtifactPublicationReceipt publication,
        IEnumerable<ProjectCodeModuleIndexEntry> entries)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendString(hash, "project-code-module-index-v1");
        AppendString(hash, publication.PublicationId);
        AppendString(
            hash,
            publication.Report.ProjectId.ToString("N"));
        AppendString(hash, publication.Report.AssemblyName);
        foreach (var entry in entries)
        {
            AppendString(hash, EntryIdentity(entry));
        }

        return "sha256-"
            + Convert.ToHexString(hash.GetHashAndReset()).ToLowerInvariant();
    }

    private static string EntryIdentity(
        ProjectCodeModuleIndexEntry entry) =>
        string.Join(
            "|",
            entry.DefinitionId.Assembly.Package.Value,
            entry.DefinitionId.Assembly.Assembly.Value,
            entry.DefinitionId.Module.Value,
            entry.DefinitionId.Scope,
            entry.TypeName,
            entry.Activation,
            entry.Handover);

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

    private static string CombinePortable(
        string root,
        string relativePath) =>
        Path.Combine(
            root,
            relativePath.Replace('/', Path.DirectorySeparatorChar));

    private static ProjectCodeModuleIndexResult Failure(
        string code,
        string location,
        string message) =>
        ProjectCodeModuleIndexResult.Failure(
            [Diagnostic(code, location, message)]);

    private static ProjectCodeModuleIndexDiagnostic Diagnostic(
        string code,
        string location,
        string message) =>
        new(code, location, message);

    private enum AttributeMatch
    {
        None,
        Exact,
        Spoofed,
    }

    private enum AttributeValueType
    {
        Unknown,
        Boolean,
        Int32,
        String,
        SystemType,
        Scope,
        Activation,
        Handover,
        Array,
    }

    private readonly record struct ModuleDeclaration(
        ModuleLocalId ModuleId,
        EditorModuleScopeKind Scope,
        EditorModuleActivationPolicy Activation,
        EditorModuleHandoverPolicy Handover);

    private sealed class AttributeTypeProvider(
        ProjectCodeAssemblyIdentity editorIdentity) :
        ICustomAttributeTypeProvider<AttributeValueType>
    {
        public AttributeValueType GetPrimitiveType(
            PrimitiveTypeCode typeCode) =>
            typeCode switch
            {
                PrimitiveTypeCode.Boolean => AttributeValueType.Boolean,
                PrimitiveTypeCode.Int32 => AttributeValueType.Int32,
                PrimitiveTypeCode.String => AttributeValueType.String,
                _ => AttributeValueType.Unknown,
            };

        public AttributeValueType GetSystemType() =>
            AttributeValueType.SystemType;

        public bool IsSystemType(AttributeValueType type) =>
            type == AttributeValueType.SystemType;

        public AttributeValueType GetSZArrayType(
            AttributeValueType elementType) =>
            AttributeValueType.Array;

        public AttributeValueType GetTypeFromDefinition(
            MetadataReader reader,
            TypeDefinitionHandle handle,
            byte rawTypeKind) =>
            AttributeValueType.Unknown;

        public AttributeValueType GetTypeFromReference(
            MetadataReader reader,
            TypeReferenceHandle handle,
            byte rawTypeKind)
        {
            var type = reader.GetTypeReference(handle);
            return MatchEnumType(
                reader.GetString(type.Namespace)
                    + "."
                    + reader.GetString(type.Name),
                IsExactTypeReference(
                    reader,
                    handle,
                    reader.GetString(type.Namespace),
                    reader.GetString(type.Name),
                    editorIdentity));
        }

        public AttributeValueType GetTypeFromSerializedName(
            string name)
        {
            var separator = name.IndexOf(',');
            var typeName = separator < 0
                ? name
                : name[..separator].Trim();
            var scopeMatches = separator < 0
                || string.Equals(
                    name[(separator + 1)..].Trim(),
                    editorIdentity.FullName,
                    StringComparison.Ordinal);
            return MatchEnumType(typeName, scopeMatches);
        }

        public PrimitiveTypeCode GetUnderlyingEnumType(
            AttributeValueType type) =>
            type is AttributeValueType.Scope
                or AttributeValueType.Activation
                or AttributeValueType.Handover
                    ? PrimitiveTypeCode.Int32
                    : throw new BadImageFormatException();

        private static AttributeValueType MatchEnumType(
            string typeName,
            bool scopeMatches)
        {
            if (!scopeMatches)
            {
                return AttributeValueType.Unknown;
            }

            return typeName switch
            {
                AttributeNamespace + ".EditorModuleScopeKind" =>
                    AttributeValueType.Scope,
                AttributeNamespace + ".EditorModuleActivationPolicy" =>
                    AttributeValueType.Activation,
                AttributeNamespace + ".EditorModuleHandoverPolicy" =>
                    AttributeValueType.Handover,
                _ => AttributeValueType.Unknown,
            };
        }
    }
}
