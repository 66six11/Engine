using System.Buffers.Binary;
using System.Reflection;
using System.Reflection.PortableExecutable;
using System.Text;
using System.Text.Json;
using System.Xml;
using System.Xml.Linq;

namespace Asharia.Studio.Distribution;

internal static class StudioEditorImageIdentityInspector
{
    private const int MaxAppHostBytes = 64 * 1024 * 1024;
    private const int MaxPortableExecutableBytes = 64 * 1024 * 1024;
    private const int MaxManagedAssemblyBytes = 64 * 1024 * 1024;
    private const int MaxJsonBytes = 8 * 1024 * 1024;
    private const int MaxXmlCharacters = 8 * 1024 * 1024;
    private const int AppBinarySlotSize = 1025;
    private const int DotNetSearchSlotSize = 512;
    private const byte AppRelativeSearchLocation = 1 << 1;
    private const int MaxExportNames = 65_536;
    private const int MaxExportNameBytes = 512;
    private const int MaxResourceEntries = 4_096;
    private const int MaxResourceNameCharacters = 512;
    private const ushort Win32VersionResourceType = 16;
    private const ushort Win32VersionResourceName = 1;
    private const int FixedFileInfoSize = 52;
    private const uint FixedFileInfoSignature = 0xfeef04bd;
    private const uint FixedFileInfoStructureVersion = 0x00010000;
    private static readonly byte[] AppBinaryPlaceholder = Encoding.ASCII.GetBytes(
        "c3ab8ff13720e8ad9047dd39466b3c8974e592c2fa383d4a3960714caef0c4f2");
    private static readonly byte[] DotNetSearchPlaceholder = CreateDotNetSearchPlaceholder();
    private static readonly Encoding StrictUtf16LittleEndian =
        new UnicodeEncoding(bigEndian: false, byteOrderMark: false, throwOnInvalidBytes: true);

    public static bool IsBoundAppHost(
        string path,
        string templatePath,
        string resourceSourcePath,
        string expectedManagedEntry,
        string expectedAppRelativeDotNet,
        out string error)
    {
        try
        {
            var contents = ReadBoundedFile(path, MaxAppHostBytes);
            var template = ReadBoundedFile(templatePath, MaxAppHostBytes);
            var resourceSource = ReadBoundedFile(
                resourceSourcePath,
                MaxManagedAssemblyBytes);
            var appSlotOffset = FindUniquePaddedSlot(
                template,
                AppBinaryPlaceholder,
                AppBinarySlotSize);
            var searchSlotOffset = FindUniquePaddedSlot(
                template,
                DotNetSearchPlaceholder,
                DotNetSearchSlotSize);
            if (appSlotOffset < 0 || searchSlotOffset < 0)
            {
                error = "Selected SDK apphost template does not contain the canonical binding slots.";
                return false;
            }

            if (!MatchesSelectedAppHostTemplate(
                    template,
                    contents,
                    resourceSource,
                    appSlotOffset,
                    searchSlotOffset,
                    out error))
            {
                return false;
            }

            var managedEntry = NullTerminatedUtf8(expectedManagedEntry);
            if (!IsExactPaddedSlot(contents, appSlotOffset, managedEntry, AppBinarySlotSize))
            {
                error = $"Apphost must bind its active app slot to '{expectedManagedEntry}'.";
                return false;
            }

            var relativeRoot = Encoding.UTF8.GetBytes(expectedAppRelativeDotNet);
            var searchOptions = new byte[relativeRoot.Length + 3];
            searchOptions[0] = AppRelativeSearchLocation;
            relativeRoot.CopyTo(searchOptions.AsSpan(2));
            if (!IsExactPaddedSlot(
                    contents,
                    searchSlotOffset,
                    searchOptions,
                    DotNetSearchSlotSize))
            {
                error = "Apphost must use the fixed AppRelative '../managed/dotnet' search binding.";
                return false;
            }

            error = string.Empty;
            return true;
        }
        catch (Exception exception) when (
            exception is BadImageFormatException
                or IOException
                or UnauthorizedAccessException
                or InvalidDataException
                or OverflowException
                or ArgumentException)
        {
            error = $"Apphost identity could not be inspected ({exception.GetType().Name}).";
            return false;
        }
    }

    public static bool HasManagedAssemblyIdentity(
        string path,
        string expectedName,
        Version expectedVersion,
        string expectedPublicKeyToken,
        out string error)
    {
        try
        {
            var file = new FileInfo(path);
            if (!file.Exists || file.Length <= 0 || file.Length > MaxManagedAssemblyBytes)
            {
                throw new InvalidDataException("Managed assembly size is outside the supported bound.");
            }

            var identity = AssemblyName.GetAssemblyName(path);
            var publicKeyToken = Convert.ToHexString(identity.GetPublicKeyToken() ?? [])
                .ToLowerInvariant();
            if (!string.Equals(identity.Name, expectedName, StringComparison.Ordinal)
                || identity.Version != expectedVersion
                || !string.IsNullOrEmpty(identity.CultureName)
                || identity.ContentType != AssemblyContentType.Default
                || !publicKeyToken.Equals(expectedPublicKeyToken, StringComparison.Ordinal))
            {
                error = $"Managed assembly identity must be neutral '{expectedName}, Version={expectedVersion}' with public-key token '{expectedPublicKeyToken}'.";
                return false;
            }

            error = string.Empty;
            return true;
        }
        catch (Exception exception) when (
            exception is BadImageFormatException
                or FileLoadException
                or FileNotFoundException
                or IOException
                or UnauthorizedAccessException
                or InvalidDataException)
        {
            error = $"Managed assembly identity could not be inspected ({exception.GetType().Name}).";
            return false;
        }
    }

    public static bool HasProductVersion(
        string path,
        string expectedVersion,
        out string error)
    {
        try
        {
            var productVersion = ReadProductVersion(path);
            if (!HasExactVersionPrefix(productVersion, expectedVersion))
            {
                error = $"Product version must identify exact component version '{expectedVersion}'.";
                return false;
            }

            error = string.Empty;
            return true;
        }
        catch (Exception exception) when (
            exception is FileNotFoundException
                or BadImageFormatException
                or IOException
                or UnauthorizedAccessException
                or InvalidDataException
                or OverflowException
                or ArgumentException)
        {
            error = $"Product version could not be inspected ({exception.GetType().Name}).";
            return false;
        }
    }

    public static bool HasRequiredExports(
        string path,
        string expectedDllName,
        IReadOnlyCollection<string> requiredExports,
        out string error)
    {
        try
        {
            var exportTable = ReadExportTable(path);
            if (!exportTable.DllName.Equals(expectedDllName, StringComparison.OrdinalIgnoreCase))
            {
                error = $"PE export identity must name '{expectedDllName}'.";
                return false;
            }

            var missing = requiredExports
                .Where(name => !exportTable.Exports.TryGetValue(name, out var entry)
                    || !entry.IsDirectExecutable)
                .Order(StringComparer.Ordinal)
                .ToArray();
            if (missing.Length != 0)
            {
                error = $"Required native exports are missing: {string.Join(", ", missing)}.";
                return false;
            }

            error = string.Empty;
            return true;
        }
        catch (Exception exception) when (
            exception is BadImageFormatException
                or IOException
                or UnauthorizedAccessException
                or InvalidDataException
                or OverflowException
                or ArgumentException)
        {
            error = $"Native export identity could not be inspected ({exception.GetType().Name}).";
            return false;
        }
    }

    public static bool HasEditorRuntimeEvidence(
        string depsPath,
        string runtimeConfigPath,
        string targetFramework,
        string selectedRuntimeVersion,
        out string error)
    {
        if (!TryParseStableVersion(selectedRuntimeVersion, out var selectedRuntime))
        {
            error = "Selected host runtime version is invalid.";
            return false;
        }

        try
        {
            using var runtimeConfig = ParseJsonFile(runtimeConfigPath);
            EnsureNoDuplicateJsonProperties(runtimeConfig.RootElement);
            var runtimeOptions = RequiredObject(runtimeConfig.RootElement, "runtimeOptions");
            if (!RequiredString(runtimeOptions, "tfm").Equals(
                    targetFramework,
                    StringComparison.Ordinal)
                || runtimeOptions.TryGetProperty("rollForward", out _)
                || runtimeOptions.TryGetProperty("applyPatches", out _)
                || runtimeOptions.TryGetProperty("rollForwardOnNoCandidateFx", out _))
            {
                error = $"Editor runtimeconfig must use the canonical '{targetFramework}' framework-dependent policy without roll-forward overrides.";
                return false;
            }

            var framework = RequiredObject(runtimeOptions, "framework");
            if (!RequiredString(framework, "name").Equals(
                    "Microsoft.NETCore.App",
                    StringComparison.Ordinal)
                || !TryParseStableVersion(RequiredString(framework, "version"), out var minimumRuntime)
                || minimumRuntime.Major != selectedRuntime.Major
                || minimumRuntime.Minor != selectedRuntime.Minor
                || minimumRuntime != new Version(selectedRuntime.Major, selectedRuntime.Minor, 0)
                || minimumRuntime > selectedRuntime)
            {
                error = "Editor runtimeconfig framework is incompatible with the selected host runtime.";
                return false;
            }

            using var deps = ParseJsonFile(depsPath);
            EnsureNoDuplicateJsonProperties(deps.RootElement);
            var targetFrameworkVersion = $"v{selectedRuntime.Major}.{selectedRuntime.Minor}";
            var expectedRuntimeTarget = $".NETCoreApp,Version={targetFrameworkVersion}/win-x64";
            var runtimeTarget = RequiredString(
                RequiredObject(deps.RootElement, "runtimeTarget"),
                "name");
            if (!runtimeTarget.Equals(expectedRuntimeTarget, StringComparison.Ordinal))
            {
                error = $"Editor deps runtime target must be '{expectedRuntimeTarget}'.";
                return false;
            }

            var target = RequiredObject(
                RequiredObject(deps.RootElement, "targets"),
                runtimeTarget);
            var libraries = RequiredObject(deps.RootElement, "libraries");
            if (!TryFindUniqueLibrary(
                    target,
                    "Editor",
                    out var editorKey,
                    out var editorVersion,
                    out var editor)
                || !editorVersion.Equals("1.0.0", StringComparison.Ordinal)
                || !HasRuntimeAsset(editor, "Editor.dll")
                || !HasDependency(editor, "Asharia.Editor", "1.0.0")
                || !HasDependency(editor, "Asharia.Runtime.Contracts", "1.0.0")
                || !HasProjectLibrary(libraries, editorKey)
                || !TryFindUniqueLibrary(
                    target,
                    "Asharia.Editor",
                    out var editorContractKey,
                    out var editorContractVersion,
                    out var editorContract)
                || !editorContractVersion.Equals("1.0.0", StringComparison.Ordinal)
                || !HasRuntimeAsset(
                    editorContract,
                    "Asharia.Editor.dll",
                    expectedAssemblyVersion: "1.0.0.0")
                || !HasProjectLibrary(libraries, editorContractKey)
                || !TryFindUniqueLibrary(
                    target,
                    "Asharia.Runtime.Contracts",
                    out var runtimeContractKey,
                    out var runtimeContractVersion,
                    out var runtimeContract)
                || !runtimeContractVersion.Equals("1.0.0", StringComparison.Ordinal)
                || !HasRuntimeAsset(
                    runtimeContract,
                    "Asharia.Runtime.Contracts.dll",
                    expectedAssemblyVersion: "1.0.0.0")
                || !HasProjectLibrary(libraries, runtimeContractKey))
            {
                error = "Editor deps must bind Editor.dll and both fixed managed contract assemblies.";
                return false;
            }

            error = string.Empty;
            return true;
        }
        catch (Exception exception) when (
            exception is IOException
                or UnauthorizedAccessException
                or InvalidDataException
                or JsonException
                or ArgumentException)
        {
            error = $"Editor managed runtime evidence is invalid ({exception.GetType().Name}).";
            return false;
        }
    }

    public static bool HasSdkRuntimeEvidence(
        string bundledVersionsPath,
        string runtimeConfigPath,
        string targetFramework,
        string selectedSdkVersion,
        string selectedRuntimeVersion,
        out string error)
    {
        if (!TryParseStableVersion(selectedRuntimeVersion, out var selectedRuntime))
        {
            error = "Selected host runtime version is invalid.";
            return false;
        }

        try
        {
            using var runtimeConfig = ParseJsonFile(runtimeConfigPath);
            EnsureNoDuplicateJsonProperties(runtimeConfig.RootElement);
            var runtimeOptions = RequiredObject(runtimeConfig.RootElement, "runtimeOptions");
            var framework = RequiredObject(runtimeOptions, "framework");
            if (!RequiredString(runtimeOptions, "tfm").Equals(
                    targetFramework,
                    StringComparison.Ordinal)
                || !RequiredString(framework, "name").Equals(
                    "Microsoft.NETCore.App",
                    StringComparison.Ordinal)
                || !RequiredString(framework, "version").Equals(
                    selectedRuntimeVersion,
                    StringComparison.Ordinal))
            {
                error = "SDK runtimeconfig must bind the selected target framework and host runtime.";
                return false;
            }

            var settings = new XmlReaderSettings
            {
                DtdProcessing = DtdProcessing.Prohibit,
                MaxCharactersInDocument = MaxXmlCharacters,
                XmlResolver = null,
            };
            using var stream = new FileStream(
                bundledVersionsPath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read);
            using var reader = XmlReader.Create(stream, settings);
            var document = XDocument.Load(reader, LoadOptions.None);
            EnsureClosedSdkEvidenceDocument(document);
            var expectedTargetFrameworkVersion =
                $"{selectedRuntime.Major}.{selectedRuntime.Minor}";
            if (!SingleProperty(document, "BundledNETCoreAppTargetFrameworkVersion")
                    .Equals(expectedTargetFrameworkVersion, StringComparison.Ordinal)
                || !SingleProperty(document, "BundledNETCoreAppPackageVersion")
                    .Equals(selectedRuntimeVersion, StringComparison.Ordinal)
                || !SingleProperty(document, "NETCoreSdkVersion")
                    .Equals(selectedSdkVersion, StringComparison.Ordinal)
                || !SingleProperty(document, "NETCoreSdkRuntimeIdentifier")
                    .Equals("win-x64", StringComparison.Ordinal)
                || !SingleProperty(document, "NETCoreSdkPortableRuntimeIdentifier")
                    .Equals("win-x64", StringComparison.Ordinal))
            {
                error = "SDK bundled runtime evidence does not match the selected host runtime.";
                return false;
            }

            error = string.Empty;
            return true;
        }
        catch (Exception exception) when (
            exception is IOException
                or UnauthorizedAccessException
                or InvalidDataException
                or JsonException
                or XmlException
                or ArgumentException)
        {
            error = $"SDK runtime evidence is invalid ({exception.GetType().Name}).";
            return false;
        }
    }

    private static byte[] ReadBoundedFile(string path, int maxBytes)
    {
        using var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read);
        if (stream.Length <= 0 || stream.Length > maxBytes)
        {
            throw new InvalidDataException("Identity file size is outside the supported bound.");
        }

        var contents = new byte[checked((int)stream.Length)];
        stream.ReadExactly(contents);
        return contents;
    }

    private static byte[] NullTerminatedUtf8(string value)
    {
        var encoded = Encoding.UTF8.GetBytes(value);
        var result = new byte[encoded.Length + 1];
        encoded.CopyTo(result, 0);
        return result;
    }

    private static int FindUniquePaddedSlot(
        ReadOnlySpan<byte> contents,
        ReadOnlySpan<byte> prefix,
        int slotSize)
    {
        if (prefix.Length == 0 || prefix.Length > slotSize)
        {
            return -1;
        }

        var found = -1;
        var cursor = 0;
        while (cursor <= contents.Length - slotSize)
        {
            var relative = contents[cursor..].IndexOf(prefix);
            if (relative < 0)
            {
                break;
            }

            var index = cursor + relative;
            if (index <= contents.Length - slotSize
                && IsExactPaddedSlot(contents, index, prefix, slotSize))
            {
                if (found >= 0)
                {
                    return -1;
                }

                found = index;
            }

            cursor = index + 1;
        }

        return found;
    }

    private static bool IsExactPaddedSlot(
        ReadOnlySpan<byte> contents,
        int offset,
        ReadOnlySpan<byte> prefix,
        int slotSize)
    {
        if (offset < 0
            || prefix.Length == 0
            || prefix.Length > slotSize
            || offset > contents.Length - slotSize
            || !contents.Slice(offset, prefix.Length).SequenceEqual(prefix))
        {
            return false;
        }

        foreach (var value in contents.Slice(
            offset + prefix.Length,
            slotSize - prefix.Length))
        {
            if (value != 0)
            {
                return false;
            }
        }

        return true;
    }

    private static byte[] CreateDotNetSearchPlaceholder()
    {
        var hash = Encoding.ASCII.GetBytes(
            "19ff3e9c3602ae8e841925bb461a0adb064a1f1903667a5e0d87e8f608f425ac");
        var result = new byte[hash.Length + 2];
        hash.CopyTo(result, 2);
        return result;
    }

    private static bool MatchesSelectedAppHostTemplate(
        byte[] template,
        byte[] candidate,
        byte[] resourceSource,
        int appSlotOffset,
        int searchSlotOffset,
        out string error)
    {
        using var templateStream = new MemoryStream(template, writable: false);
        using var candidateStream = new MemoryStream(candidate, writable: false);
        using var resourceSourceStream = new MemoryStream(resourceSource, writable: false);
        using var templateReader = new PEReader(templateStream, PEStreamOptions.LeaveOpen);
        using var candidateReader = new PEReader(candidateStream, PEStreamOptions.LeaveOpen);
        using var resourceSourceReader = new PEReader(
            resourceSourceStream,
            PEStreamOptions.LeaveOpen);
        var templateHeaders = templateReader.PEHeaders;
        var candidateHeaders = candidateReader.PEHeaders;
        var resourceSourceHeaders = resourceSourceReader.PEHeaders;
        var templatePe = templateHeaders.PEHeader;
        var candidatePe = candidateHeaders.PEHeader;
        var resourceSourcePe = resourceSourceHeaders.PEHeader;
        if (templatePe is null
            || candidatePe is null
            || resourceSourcePe is null
            || templateReader.HasMetadata
            || candidateReader.HasMetadata
            || !resourceSourceReader.HasMetadata
            || templateHeaders.CoffHeader.Machine != Machine.Amd64
            || candidateHeaders.CoffHeader.Machine != Machine.Amd64
            || templatePe.Magic != PEMagic.PE32Plus
            || candidatePe.Magic != PEMagic.PE32Plus
            || (templateHeaders.CoffHeader.Characteristics & Characteristics.Dll) != 0
            || (candidateHeaders.CoffHeader.Characteristics & Characteristics.Dll) != 0
            || candidatePe.Subsystem != Subsystem.WindowsGui)
        {
            error = "Apphost must be one native Windows x64 GUI PE32+ executable.";
            return false;
        }

        if (templatePe.AddressOfEntryPoint != candidatePe.AddressOfEntryPoint
            || templatePe.ImageBase != candidatePe.ImageBase
            || templatePe.SectionAlignment != candidatePe.SectionAlignment
            || templatePe.FileAlignment != candidatePe.FileAlignment
            || templatePe.SizeOfHeaders != candidatePe.SizeOfHeaders
            || templatePe.MajorOperatingSystemVersion != candidatePe.MajorOperatingSystemVersion
            || templatePe.MinorOperatingSystemVersion != candidatePe.MinorOperatingSystemVersion
            || templatePe.MajorSubsystemVersion != candidatePe.MajorSubsystemVersion
            || templatePe.MinorSubsystemVersion != candidatePe.MinorSubsystemVersion
            || templatePe.DllCharacteristics != candidatePe.DllCharacteristics)
        {
            error = "Apphost PE identity does not match the selected SDK template.";
            return false;
        }

        var templateSections = templateHeaders.SectionHeaders.ToArray();
        var candidateSections = candidateHeaders.SectionHeaders.ToArray();
        var resourceSourceSections = resourceSourceHeaders.SectionHeaders.ToArray();
        var candidateResources = candidateSections
            .Where(section => section.Name.Equals(".rsrc", StringComparison.Ordinal))
            .ToArray();
        var sourceResources = resourceSourceSections
            .Where(section => section.Name.Equals(".rsrc", StringComparison.Ordinal))
            .ToArray();
        if (templateSections.Any(section => section.Name.Equals(".rsrc", StringComparison.Ordinal))
            || candidateResources.Length != 1
            || sourceResources.Length != 1
            || candidateSections.Length != templateSections.Length + 1
            || !candidateSections[^1].Name.Equals(".rsrc", StringComparison.Ordinal))
        {
            error = "Apphost section layout does not match the selected SDK template.";
            return false;
        }

        for (var index = 0; index < templateSections.Length; ++index)
        {
            var expected = templateSections[index];
            var actual = candidateSections[index];
            if (!HasSameSectionGeometry(expected, actual))
            {
                error = $"Apphost section '{expected.Name}' differs from the selected SDK template.";
                return false;
            }
        }

        var resource = candidateResources[0];
        var sourceResource = sourceResources[0];
        var templateCertificate = templatePe.CertificateTableDirectory;
        if ((resource.SectionCharacteristics & SectionCharacteristics.MemExecute) != 0
            || resource.SectionCharacteristics
                != (SectionCharacteristics.ContainsInitializedData
                    | SectionCharacteristics.MemRead)
            || !RvaBelongsToSection(
                candidatePe.ResourceTableDirectory.RelativeVirtualAddress,
                candidatePe.ResourceTableDirectory.Size,
                resource)
            || !RvaBelongsToSection(
                resourceSourcePe.ResourceTableDirectory.RelativeVirtualAddress,
                resourceSourcePe.ResourceTableDirectory.Size,
                sourceResource)
            || candidatePe.ResourceTableDirectory.RelativeVirtualAddress
                != resource.VirtualAddress
            || candidatePe.ResourceTableDirectory.Size != resource.VirtualSize)
        {
            error = "Apphost resource section is invalid.";
            return false;
        }

        if (!HasEquivalentWin32Resources(
                candidate,
                resource,
                resourceSource,
                resourceSourceHeaders,
                sourceResource))
        {
            error = "Apphost resources do not match the fixed Editor.dll resource tree.";
            return false;
        }

        var templateLayout = ReadPeLayout(template);
        var candidateLayout = ReadPeLayout(candidate);
        var expectedResourceHeaderOffset = checked(
            templateLayout.SectionHeadersOffset + templateSections.Length * 40);
        var lastTemplateSection = templateSections[^1];
        var expectedResourceRawPointer = AlignUp(
            checked(lastTemplateSection.PointerToRawData + lastTemplateSection.SizeOfRawData),
            templatePe.FileAlignment);
        var expectedResourceVirtualAddress = AlignUp(
            checked(lastTemplateSection.VirtualAddress + lastTemplateSection.VirtualSize),
            templatePe.SectionAlignment);
        var expectedResourceRawSize = AlignUp(
            resource.VirtualSize,
            templatePe.FileAlignment);
        var expectedImageSize = checked(
            resource.VirtualAddress
                + AlignUp(resource.VirtualSize, templatePe.SectionAlignment));
        if (templateLayout.PeSignatureOffset != candidateLayout.PeSignatureOffset
            || templateLayout.OptionalHeaderOffset != candidateLayout.OptionalHeaderOffset
            || templateLayout.OptionalHeaderSize != candidateLayout.OptionalHeaderSize
            || templateLayout.SectionHeadersOffset != candidateLayout.SectionHeadersOffset
            || expectedResourceHeaderOffset > templatePe.SizeOfHeaders - 40
            || !IsAllZero(template.AsSpan(expectedResourceHeaderOffset, 40))
            || resource.PointerToRawData != expectedResourceRawPointer
            || resource.PointerToRawData != template.Length
            || resource.VirtualAddress != expectedResourceVirtualAddress
            || resource.SizeOfRawData != expectedResourceRawSize
            || candidatePe.SizeOfInitializedData
                != checked(templatePe.SizeOfInitializedData + resource.SizeOfRawData)
            || candidatePe.SizeOfImage != expectedImageSize
            || !HasCanonicalResourceSectionHeader(
                candidate,
                expectedResourceHeaderOffset,
                resource)
            || !IsAllZero(candidate.AsSpan(
                checked(resource.PointerToRawData + resource.VirtualSize),
                resource.SizeOfRawData - resource.VirtualSize)))
        {
            error = "Apphost resource updater output is not canonical.";
            return false;
        }

        var certificate = candidatePe.CertificateTableDirectory;
        if (templateCertificate.RelativeVirtualAddress != 0
            || templateCertificate.Size != 0
            || certificate.RelativeVirtualAddress != 0
            || certificate.Size != 0
            || !HasClosedSectionLayout(template, templatePe.SizeOfHeaders, templateSections)
            || !HasClosedSectionLayout(candidate, candidatePe.SizeOfHeaders, candidateSections))
        {
            error = "Apphost must not contain a certificate or single-file bundle overlay.";
            return false;
        }

        var allowedDifferences = new[]
        {
            new ByteRange(appSlotOffset, AppBinarySlotSize),
            new ByteRange(searchSlotOffset, DotNetSearchSlotSize),
            // ResourceUpdater writes a 32-bit section count at the 16-bit COFF
            // field, so the timestamp's low word is deterministically cleared.
            new ByteRange(templateLayout.CoffHeaderOffset + 2, 4),
            new ByteRange(templateLayout.OptionalHeaderOffset + 8, 4),
            new ByteRange(templateLayout.OptionalHeaderOffset + 56, 4),
            new ByteRange(templateLayout.OptionalHeaderOffset + 68, 2),
            new ByteRange(templateLayout.OptionalHeaderOffset + 112 + 2 * 8, 8),
            new ByteRange(expectedResourceHeaderOffset, 40),
        };
        if (ReadUInt32(candidate, candidateLayout.CoffHeaderOffset + 4)
                != (ReadUInt32(template, templateLayout.CoffHeaderOffset + 4)
                    & 0xffff0000u)
            || !BytesMatchExceptRanges(template, candidate, allowedDifferences))
        {
            error = "Apphost contains a non-resource difference from the selected SDK template.";
            return false;
        }

        error = string.Empty;
        return true;
    }

    private static bool HasSameSectionGeometry(SectionHeader expected, SectionHeader actual) =>
        expected.Name.Equals(actual.Name, StringComparison.Ordinal)
        && expected.VirtualAddress == actual.VirtualAddress
        && expected.VirtualSize == actual.VirtualSize
        && expected.PointerToRawData == actual.PointerToRawData
        && expected.SizeOfRawData == actual.SizeOfRawData
        && expected.SectionCharacteristics == actual.SectionCharacteristics;

    private static bool HasCanonicalResourceSectionHeader(
        byte[] contents,
        int offset,
        SectionHeader resource)
    {
        EnsureRange(contents, offset, 40);
        return contents.AsSpan(offset, 8).SequenceEqual(".rsrc\0\0\0"u8)
            && ReadInt32(contents, offset + 8) == resource.VirtualSize
            && ReadInt32(contents, offset + 12) == resource.VirtualAddress
            && ReadInt32(contents, offset + 16) == resource.SizeOfRawData
            && ReadInt32(contents, offset + 20) == resource.PointerToRawData
            && ReadUInt32(contents, offset + 24) == 0
            && ReadUInt32(contents, offset + 28) == 0
            && ReadUInt16(contents, offset + 32) == 0
            && ReadUInt16(contents, offset + 34) == 0
            && ReadUInt32(contents, offset + 36) == 0x40000040;
    }

    private static bool HasEquivalentWin32Resources(
        byte[] candidate,
        SectionHeader candidateResource,
        byte[] resourceSource,
        PEHeaders resourceSourceHeaders,
        SectionHeader sourceResource)
    {
        var sourceResources = Win32ResourceReader.Read(
            resourceSource,
            resourceSourceHeaders,
            sourceResource);
        if (sourceResources.Count == 0)
        {
            return false;
        }

        var expected = BuildCanonicalWin32Resources(
            resourceSource,
            sourceResources,
            candidateResource.VirtualAddress);
        return expected.Length == candidateResource.VirtualSize
            && candidate.AsSpan(candidateResource.PointerToRawData, expected.Length)
                .SequenceEqual(expected);
    }

    private static byte[] BuildCanonicalWin32Resources(
        byte[] source,
        IReadOnlyDictionary<Win32ResourceKey, Win32ResourceValue> resources,
        int sectionBase)
    {
        var builder = new CanonicalResourceBuilder();
        var types = OrderResourceIdentifiers(resources.Keys.Select(key => key.Type))
            .Select(type => new CanonicalResourceType(
                type,
                OrderResourceIdentifiers(resources.Keys
                        .Where(key => key.Type == type)
                        .Select(key => key.Name))
                    .Select(name => new CanonicalResourceName(
                        name,
                        resources
                            .Where(resource => resource.Key.Type == type
                                && resource.Key.Name == name)
                            .OrderBy(resource => resource.Key.Language.Id)
                            .ToArray()))
                    .ToArray()))
            .ToArray();
        var nameReferences = new SortedDictionary<string, List<int>>();
        var typeReferences = new List<(CanonicalResourceType Type, int Target)>();
        var nameEntries = new List<(CanonicalResourceName Name, int Target)>();
        var languageEntries = new List<(Win32ResourceValue Value, int Target)>();

        WriteCanonicalResourceDirectory(
            builder,
            types.Count(type => type.Identifier.Name is not null),
            types.Count(type => type.Identifier.Name is null));
        foreach (var type in types)
        {
            typeReferences.Add((
                type,
                WriteCanonicalResourceEntry(
                    builder,
                    type.Identifier,
                    nameReferences)));
        }

        foreach (var (type, target) in typeReferences)
        {
            builder.PatchUInt32(target, checked((uint)builder.Count) | 0x80000000u);
            WriteCanonicalResourceDirectory(
                builder,
                type.Names.Count(name => name.Identifier.Name is not null),
                type.Names.Count(name => name.Identifier.Name is null));
            foreach (var name in type.Names)
            {
                nameEntries.Add((
                    name,
                    WriteCanonicalResourceEntry(
                        builder,
                        name.Identifier,
                        nameReferences)));
            }
        }

        foreach (var (name, target) in nameEntries)
        {
            builder.PatchUInt32(target, checked((uint)builder.Count) | 0x80000000u);
            WriteCanonicalResourceDirectory(builder, 0, name.Resources.Length);
            foreach (var resource in name.Resources)
            {
                builder.WriteUInt32(resource.Key.Language.Id);
                languageEntries.Add((resource.Value, builder.ReserveUInt32()));
            }
        }

        builder.Align(2);
        foreach (var (name, references) in nameReferences)
        {
            foreach (var reference in references)
            {
                builder.PatchUInt32(
                    reference,
                    checked((uint)builder.Count) | 0x80000000u);
            }

            builder.WriteUInt16(checked((ushort)name.Length));
            foreach (var value in name)
            {
                builder.WriteUInt16(value);
            }
        }

        var payloadOffsets = new int[languageEntries.Count];
        for (var index = 0; index < languageEntries.Count; ++index)
        {
            builder.Align(4);
            payloadOffsets[index] = builder.Count;
            var value = languageEntries[index].Value;
            builder.WriteBytes(source.AsSpan(value.Offset, value.Size));
        }

        builder.Align(4);
        for (var index = 0; index < languageEntries.Count; ++index)
        {
            var (value, target) = languageEntries[index];
            builder.PatchUInt32(target, checked((uint)builder.Count));
            builder.WriteUInt32(checked((uint)(sectionBase + payloadOffsets[index])));
            builder.WriteUInt32(checked((uint)value.Size));
            builder.WriteUInt32(1252);
            builder.WriteUInt32(0);
        }

        builder.Align(4);
        return builder.ToArray();
    }

    private static Win32ResourceIdentifier[] OrderResourceIdentifiers(
        IEnumerable<Win32ResourceIdentifier> identifiers) => identifiers
        .Distinct()
        .Where(identifier => identifier.Name is not null)
        .OrderBy(identifier => identifier.Name!, Comparer<string>.Default)
        .Concat(identifiers
            .Distinct()
            .Where(identifier => identifier.Name is null)
            .OrderBy(identifier => identifier.Id))
        .ToArray();

    private static int WriteCanonicalResourceEntry(
        CanonicalResourceBuilder builder,
        Win32ResourceIdentifier identifier,
        IDictionary<string, List<int>> nameReferences)
    {
        if (identifier.Name is null)
        {
            builder.WriteUInt32(identifier.Id);
        }
        else
        {
            var reference = builder.ReserveUInt32();
            if (!nameReferences.TryGetValue(identifier.Name, out var references))
            {
                references = [];
                nameReferences.Add(identifier.Name, references);
            }

            references.Add(reference);
        }

        return builder.ReserveUInt32();
    }

    private static void WriteCanonicalResourceDirectory(
        CanonicalResourceBuilder builder,
        int namedCount,
        int idCount)
    {
        builder.WriteUInt32(0);
        builder.WriteUInt32(0);
        builder.WriteUInt16(4);
        builder.WriteUInt16(0);
        builder.WriteUInt16(checked((ushort)namedCount));
        builder.WriteUInt16(checked((ushort)idCount));
    }

    private static bool BytesMatchExceptRanges(
        byte[] expected,
        byte[] actual,
        IReadOnlyCollection<ByteRange> allowedDifferences)
    {
        if (actual.Length < expected.Length)
        {
            return false;
        }

        foreach (var range in allowedDifferences)
        {
            EnsureRange(expected, range.Offset, range.Length);
        }

        for (var offset = 0; offset < expected.Length; ++offset)
        {
            if (allowedDifferences.Any(range => IsWithin(
                    offset,
                    range.Offset,
                    range.Length)))
            {
                continue;
            }

            if (expected[offset] != actual[offset])
            {
                return false;
            }
        }

        return true;
    }

    private static PeLayout ReadPeLayout(byte[] contents)
    {
        var peSignatureOffset = checked((int)ReadUInt32(contents, 0x3c));
        EnsureRange(contents, peSignatureOffset, 4 + 20);
        if (!contents.AsSpan(peSignatureOffset, 4).SequenceEqual("PE\0\0"u8))
        {
            throw new BadImageFormatException();
        }

        var coffHeaderOffset = checked(peSignatureOffset + 4);
        var optionalHeaderOffset = checked(coffHeaderOffset + 20);
        var optionalHeaderSize = ReadUInt16(contents, coffHeaderOffset + 16);
        EnsureRange(contents, optionalHeaderOffset, optionalHeaderSize);
        var sectionHeadersOffset = checked(optionalHeaderOffset + optionalHeaderSize);
        return new PeLayout(
            peSignatureOffset,
            coffHeaderOffset,
            optionalHeaderOffset,
            optionalHeaderSize,
            sectionHeadersOffset);
    }

    private static int AlignUp(int value, int alignment)
    {
        if (value < 0 || alignment <= 0 || (alignment & (alignment - 1)) != 0)
        {
            throw new InvalidDataException("PE alignment is invalid.");
        }

        return checked((value + alignment - 1) & -alignment);
    }

    private static bool IsAllZero(ReadOnlySpan<byte> contents)
    {
        foreach (var value in contents)
        {
            if (value != 0)
            {
                return false;
            }
        }

        return true;
    }

    private static bool HasClosedSectionLayout(
        byte[] contents,
        int sizeOfHeaders,
        IReadOnlyCollection<SectionHeader> sections)
    {
        var rawEnd = sizeOfHeaders;
        foreach (var section in sections.OrderBy(section => section.PointerToRawData))
        {
            if (section.SizeOfRawData == 0)
            {
                continue;
            }

            if (section.PointerToRawData < rawEnd)
            {
                return false;
            }

            rawEnd = checked(section.PointerToRawData + section.SizeOfRawData);
            EnsureRange(contents, section.PointerToRawData, section.SizeOfRawData);
        }

        return rawEnd == contents.Length;
    }

    private static bool RvaBelongsToSection(int rva, int size, SectionHeader section)
    {
        if (rva <= 0 || size <= 0)
        {
            return false;
        }

        var relative = rva - section.VirtualAddress;
        return relative >= 0
            && relative <= section.SizeOfRawData - size
            && relative <= section.VirtualSize - size;
    }

    private static bool IsWithin(int value, int offset, int length) =>
        value >= offset && value < checked(offset + length);

    private static bool HasExactVersionPrefix(string? productVersion, string expectedVersion)
    {
        if (string.IsNullOrWhiteSpace(productVersion)
            || !productVersion.StartsWith(expectedVersion, StringComparison.Ordinal))
        {
            return false;
        }

        if (productVersion.Length == expectedVersion.Length)
        {
            return true;
        }

        return productVersion[expectedVersion.Length] is '-' or '+' or ' ';
    }

    private static string ReadProductVersion(string path)
    {
        var contents = ReadBoundedFile(path, MaxPortableExecutableBytes);
        using var stream = new MemoryStream(contents, writable: false);
        using var reader = new PEReader(stream, PEStreamOptions.LeaveOpen);
        var headers = reader.PEHeaders;
        var peHeader = headers.PEHeader
            ?? throw new BadImageFormatException("PE header is missing.");
        var directory = peHeader.ResourceTableDirectory;
        if (directory.RelativeVirtualAddress == 0 || directory.Size <= 0)
        {
            throw new InvalidDataException("PE version resource is missing.");
        }

        var resourceSections = headers.SectionHeaders
            .Where(section => RvaBelongsToSection(
                directory.RelativeVirtualAddress,
                directory.Size,
                section))
            .ToArray();
        if (resourceSections.Length != 1)
        {
            throw new InvalidDataException("PE resource section is missing or ambiguous.");
        }

        var resources = Win32ResourceReader.Read(
            contents,
            headers,
            resourceSections[0]);
        var versionResourceEntries = resources
            .Where(resource => resource.Key.Type.Name is null
                && resource.Key.Type.Id == Win32VersionResourceType)
            .ToArray();
        if (versionResourceEntries.Length == 0
            || versionResourceEntries.Any(resource =>
                resource.Key.Name.Name is not null
                || resource.Key.Name.Id != Win32VersionResourceName))
        {
            throw new InvalidDataException(
                "PE version resource name is missing or invalid.");
        }

        string? productVersion = null;
        foreach (var resource in versionResourceEntries.Select(entry => entry.Value))
        {
            var current = ReadProductVersion(contents, resource);
            if (productVersion is not null
                && !productVersion.Equals(current, StringComparison.Ordinal))
            {
                throw new InvalidDataException(
                    "PE version resources disagree about ProductVersion.");
            }

            productVersion = current;
        }

        return productVersion!;
    }

    private static string ReadProductVersion(
        byte[] contents,
        Win32ResourceValue resource)
    {
        EnsureRange(contents, resource.Offset, resource.Size);
        var resourceEnd = checked(resource.Offset + resource.Size);
        var root = ReadVersionInfoBlock(contents, resource.Offset, resourceEnd);
        if (!root.Key.Equals("VS_VERSION_INFO", StringComparison.Ordinal)
            || root.Type != 0
            || root.ValueByteLength != FixedFileInfoSize
            || ReadUInt32(contents, root.ValueOffset) != FixedFileInfoSignature
            || ReadUInt32(contents, root.ValueOffset + sizeof(uint))
                != FixedFileInfoStructureVersion
            || root.End != resourceEnd)
        {
            throw new InvalidDataException("PE VS_VERSION_INFO root is invalid.");
        }

        var stringFileInfoBlocks = ReadVersionInfoChildren(contents, root)
            .Where(child => child.Key.Equals("StringFileInfo", StringComparison.Ordinal))
            .ToArray();
        if (stringFileInfoBlocks.Length != 1
            || stringFileInfoBlocks[0].Type != 1
            || stringFileInfoBlocks[0].ValueByteLength != 0)
        {
            throw new InvalidDataException("PE StringFileInfo block is missing or ambiguous.");
        }

        var productVersions = new List<string>();
        foreach (var table in ReadVersionInfoChildren(contents, stringFileInfoBlocks[0]))
        {
            if (table.Type != 1
                || table.ValueByteLength != 0
                || table.Key.Length != 8
                || table.Key.Any(character => !Uri.IsHexDigit(character)))
            {
                throw new InvalidDataException("PE StringTable block is invalid.");
            }

            var matches = ReadVersionInfoChildren(contents, table)
                .Where(value => value.Key.Equals("ProductVersion", StringComparison.Ordinal))
                .ToArray();
            if (matches.Length != 1)
            {
                throw new InvalidDataException(
                    "PE ProductVersion string is missing or ambiguous.");
            }

            var match = matches[0];
            if (match.Type != 1
                || match.ValueByteLength < sizeof(char)
                || match.ChildrenOffset != match.End)
            {
                throw new InvalidDataException("PE ProductVersion string is invalid.");
            }

            var value = StrictUtf16LittleEndian.GetString(
                contents,
                match.ValueOffset,
                match.ValueByteLength);
            if (value[^1] != '\0'
                || value.AsSpan(0, value.Length - 1).Contains('\0'))
            {
                throw new InvalidDataException(
                    "PE ProductVersion string termination is invalid.");
            }

            productVersions.Add(value[..^1]);
        }

        if (productVersions.Count == 0
            || productVersions.Any(value => !value.Equals(
                productVersions[0],
                StringComparison.Ordinal)))
        {
            throw new InvalidDataException(
                "PE StringTable blocks disagree about ProductVersion.");
        }

        return productVersions[0];
    }

    private static VersionInfoBlock ReadVersionInfoBlock(
        byte[] contents,
        int offset,
        int limit)
    {
        if ((offset & 3) != 0 || limit < offset || limit > contents.Length)
        {
            throw new InvalidDataException("PE version block range is invalid.");
        }

        EnsureRange(contents, offset, 6);
        var length = ReadUInt16(contents, offset);
        var valueLength = ReadUInt16(contents, offset + 2);
        var type = ReadUInt16(contents, offset + 4);
        if (length < 6 || type > 1 || length > limit - offset)
        {
            throw new InvalidDataException("PE version block header is invalid.");
        }

        var end = checked(offset + length);
        var keyOffset = checked(offset + 6);
        var keyEnd = FindNullTerminatedUtf16End(contents, keyOffset, end);
        var key = StrictUtf16LittleEndian.GetString(
            contents,
            keyOffset,
            keyEnd - keyOffset - sizeof(char));
        if (key.Length == 0 || key.Length > MaxResourceNameCharacters)
        {
            throw new InvalidDataException("PE version block key is invalid.");
        }

        var valueOffset = AlignUp(keyEnd, 4);
        if (valueOffset > end
            || !IsAllZero(contents.AsSpan(keyEnd, valueOffset - keyEnd)))
        {
            throw new InvalidDataException("PE version block value alignment is invalid.");
        }

        var valueByteLength = type == 1
            ? checked((int)valueLength * sizeof(char))
            : valueLength;
        if (valueByteLength > end - valueOffset)
        {
            throw new InvalidDataException("PE version block value is outside its block.");
        }

        var valueEnd = checked(valueOffset + valueByteLength);
        var childrenOffset = valueEnd == end ? end : AlignUp(valueEnd, 4);
        if (childrenOffset > end
            || !IsAllZero(contents.AsSpan(valueEnd, childrenOffset - valueEnd)))
        {
            throw new InvalidDataException("PE version block child alignment is invalid.");
        }

        return new VersionInfoBlock(
            offset,
            end,
            valueOffset,
            valueByteLength,
            childrenOffset,
            type,
            key);
    }

    private static VersionInfoBlock[] ReadVersionInfoChildren(
        byte[] contents,
        VersionInfoBlock parent)
    {
        var children = new List<VersionInfoBlock>();
        var cursor = parent.ChildrenOffset;
        while (cursor < parent.End)
        {
            var child = ReadVersionInfoBlock(contents, cursor, parent.End);
            children.Add(child);
            if (child.End == parent.End)
            {
                cursor = parent.End;
                continue;
            }

            var next = AlignUp(child.End, 4);
            if (next > parent.End
                || !IsAllZero(contents.AsSpan(child.End, next - child.End)))
            {
                throw new InvalidDataException(
                    "PE version child alignment is invalid.");
            }

            cursor = next;
        }

        return children.ToArray();
    }

    private static int FindNullTerminatedUtf16End(
        byte[] contents,
        int offset,
        int limit)
    {
        for (var cursor = offset; cursor <= limit - sizeof(char); cursor += sizeof(char))
        {
            if (ReadUInt16(contents, cursor) == 0)
            {
                return checked(cursor + sizeof(char));
            }
        }

        throw new InvalidDataException("PE version block key is not terminated.");
    }

    private static ExportTable ReadExportTable(string path)
    {
        var contents = ReadBoundedFile(path, MaxPortableExecutableBytes);
        using var stream = new MemoryStream(contents, writable: false);
        using var reader = new PEReader(stream, PEStreamOptions.LeaveOpen);
        var headers = reader.PEHeaders;
        var directory = headers.PEHeader?.ExportTableDirectory
            ?? throw new BadImageFormatException();
        if (directory.RelativeVirtualAddress == 0 || directory.Size < 40)
        {
            throw new InvalidDataException("PE export directory is missing.");
        }

        var directoryOffset = RvaToOffset(headers, directory.RelativeVirtualAddress, contents.Length);
        EnsureRange(contents, directoryOffset, 40);
        var dllNameRva = ReadUInt32(contents, directoryOffset + 12);
        var functionCount = ReadUInt32(contents, directoryOffset + 20);
        var count = ReadUInt32(contents, directoryOffset + 24);
        var functionsRva = ReadUInt32(contents, directoryOffset + 28);
        var namesRva = ReadUInt32(contents, directoryOffset + 32);
        var ordinalsRva = ReadUInt32(contents, directoryOffset + 36);
        if (functionCount == 0
            || functionCount > MaxExportNames
            || count == 0
            || count > MaxExportNames)
        {
            throw new InvalidDataException("PE export table counts are invalid.");
        }

        var dllNameOffset = RvaToOffset(headers, checked((int)dllNameRva), contents.Length);
        var dllName = ReadNullTerminatedAscii(contents, dllNameOffset);
        var functionsOffset = RvaToOffset(headers, checked((int)functionsRva), contents.Length);
        var namesOffset = RvaToOffset(headers, checked((int)namesRva), contents.Length);
        var ordinalsOffset = RvaToOffset(headers, checked((int)ordinalsRva), contents.Length);
        EnsureRange(contents, functionsOffset, checked((int)functionCount * sizeof(uint)));
        EnsureRange(contents, namesOffset, checked((int)count * sizeof(uint)));
        EnsureRange(contents, ordinalsOffset, checked((int)count * sizeof(ushort)));
        var exports = new Dictionary<string, ExportEntry>(StringComparer.Ordinal);
        var exportDirectoryStart = checked((uint)directory.RelativeVirtualAddress);
        var exportDirectoryEnd = checked(exportDirectoryStart + (uint)directory.Size);
        string? previousName = null;
        for (var index = 0; index < count; ++index)
        {
            var nameRva = ReadUInt32(contents, namesOffset + checked((int)index * sizeof(uint)));
            var nameOffset = RvaToOffset(headers, checked((int)nameRva), contents.Length);
            var name = ReadNullTerminatedAscii(contents, nameOffset);
            if (previousName is not null
                && StringComparer.Ordinal.Compare(previousName, name) >= 0)
            {
                throw new InvalidDataException(
                    "PE export name pointer table must be strictly sorted and unique.");
            }

            previousName = name;

            var ordinal = ReadUInt16(
                contents,
                ordinalsOffset + checked((int)index * sizeof(ushort)));
            if (ordinal >= functionCount)
            {
                throw new InvalidDataException("PE export ordinal is outside the function table.");
            }

            var functionRva = ReadUInt32(
                contents,
                functionsOffset + checked(ordinal * sizeof(uint)));
            var isForwarder = functionRva >= exportDirectoryStart
                && functionRva < exportDirectoryEnd;
            var isDirectExecutable = functionRva != 0
                && !isForwarder
                && IsExecutableInitializedRva(headers, functionRva, contents.Length);
            exports.Add(name, new ExportEntry(functionRva, isDirectExecutable));
        }

        return new ExportTable(dllName, exports);
    }

    private static bool IsExecutableInitializedRva(
        PEHeaders headers,
        uint rva,
        int fileLength)
    {
        if (rva > int.MaxValue)
        {
            return false;
        }

        foreach (var section in headers.SectionHeaders)
        {
            var relative = checked((int)rva - section.VirtualAddress);
            if (relative < 0
                || relative >= section.SizeOfRawData
                || (section.SectionCharacteristics & SectionCharacteristics.MemExecute) == 0)
            {
                continue;
            }

            var offset = checked(section.PointerToRawData + relative);
            EnsureRange(fileLength, offset, 1);
            return true;
        }

        return false;
    }

    private static int RvaToOffset(PEHeaders headers, int rva, int fileLength)
    {
        if (rva < 0)
        {
            throw new InvalidDataException("PE RVA is invalid.");
        }

        if (rva < headers.PEHeader!.SizeOfHeaders)
        {
            EnsureRange(fileLength, rva, 1);
            return rva;
        }

        foreach (var section in headers.SectionHeaders)
        {
            var relative = rva - section.VirtualAddress;
            var span = Math.Max(section.VirtualSize, section.SizeOfRawData);
            if (relative < 0 || relative >= span || relative >= section.SizeOfRawData)
            {
                continue;
            }

            var offset = checked(section.PointerToRawData + relative);
            EnsureRange(fileLength, offset, 1);
            return offset;
        }

        throw new InvalidDataException("PE RVA does not map to file data.");
    }

    private static uint ReadUInt32(byte[] contents, int offset)
    {
        EnsureRange(contents, offset, sizeof(uint));
        return BinaryPrimitives.ReadUInt32LittleEndian(contents.AsSpan(offset, sizeof(uint)));
    }

    private static int ReadInt32(byte[] contents, int offset)
    {
        EnsureRange(contents, offset, sizeof(int));
        return BinaryPrimitives.ReadInt32LittleEndian(contents.AsSpan(offset, sizeof(int)));
    }

    private static ushort ReadUInt16(byte[] contents, int offset)
    {
        EnsureRange(contents, offset, sizeof(ushort));
        return BinaryPrimitives.ReadUInt16LittleEndian(
            contents.AsSpan(offset, sizeof(ushort)));
    }

    private static string ReadNullTerminatedAscii(byte[] contents, int offset)
    {
        var length = 0;
        while (length < MaxExportNameBytes
            && offset + length < contents.Length
            && contents[offset + length] != 0)
        {
            var value = contents[offset + length];
            if (value is < 0x21 or > 0x7e)
            {
                throw new InvalidDataException("PE export name is not portable ASCII.");
            }

            ++length;
        }

        if (length == 0
            || length == MaxExportNameBytes
            || offset + length >= contents.Length)
        {
            throw new InvalidDataException("PE export name is invalid.");
        }

        return Encoding.ASCII.GetString(contents, offset, length);
    }

    private static void EnsureRange(byte[] contents, int offset, int length) =>
        EnsureRange(contents.Length, offset, length);

    private static void EnsureRange(int fileLength, int offset, int length)
    {
        if (offset < 0 || length < 0 || offset > fileLength - length)
        {
            throw new InvalidDataException("Identity data range is invalid.");
        }
    }

    private static JsonDocument ParseJsonFile(string path)
    {
        var contents = ReadBoundedFile(path, MaxJsonBytes);
        return JsonDocument.Parse(
            contents,
            new JsonDocumentOptions
            {
                AllowTrailingCommas = false,
                CommentHandling = JsonCommentHandling.Disallow,
                MaxDepth = 64,
            });
    }

    private static JsonElement RequiredObject(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out var value)
            || value.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException($"Required JSON object '{name}' is missing.");
        }

        return value;
    }

    private static string RequiredString(JsonElement parent, string name)
    {
        if (!parent.TryGetProperty(name, out var value)
            || value.ValueKind != JsonValueKind.String
            || string.IsNullOrEmpty(value.GetString()))
        {
            throw new InvalidDataException($"Required JSON string '{name}' is missing.");
        }

        return value.GetString()!;
    }

    private static void EnsureNoDuplicateJsonProperties(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in element.EnumerateObject())
            {
                if (!names.Add(property.Name))
                {
                    throw new InvalidDataException(
                        $"JSON property '{property.Name}' must not be repeated.");
                }

                EnsureNoDuplicateJsonProperties(property.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in element.EnumerateArray())
            {
                EnsureNoDuplicateJsonProperties(item);
            }
        }
    }

    private static bool TryFindUniqueLibrary(
        JsonElement target,
        string name,
        out string key,
        out string version,
        out JsonElement value)
    {
        key = string.Empty;
        version = string.Empty;
        value = default;
        var prefix = name + "/";
        var count = 0;
        foreach (var property in target.EnumerateObject())
        {
            if (!property.Name.StartsWith(prefix, StringComparison.Ordinal))
            {
                continue;
            }

            key = property.Name;
            version = property.Name[prefix.Length..];
            value = property.Value;
            ++count;
        }

        return count == 1
            && !string.IsNullOrEmpty(version)
            && !version.Contains("/", StringComparison.Ordinal)
            && value.ValueKind == JsonValueKind.Object;
    }

    private static bool HasRuntimeAsset(
        JsonElement library,
        string asset,
        string? expectedAssemblyVersion = null)
    {
        if (!library.TryGetProperty("runtime", out var runtime)
            || runtime.ValueKind != JsonValueKind.Object)
        {
            return false;
        }

        if (!runtime.TryGetProperty(asset, out var value)
            || value.ValueKind != JsonValueKind.Object)
        {
            return false;
        }

        return expectedAssemblyVersion is null
            || (value.TryGetProperty("assemblyVersion", out var assemblyVersion)
                && assemblyVersion.ValueKind == JsonValueKind.String
                && expectedAssemblyVersion.Equals(
                    assemblyVersion.GetString(),
                    StringComparison.Ordinal));
    }

    private static bool HasDependency(
        JsonElement library,
        string dependency,
        string expectedVersion)
    {
        return library.TryGetProperty("dependencies", out var dependencies)
            && dependencies.ValueKind == JsonValueKind.Object
            && dependencies.TryGetProperty(dependency, out var version)
            && version.ValueKind == JsonValueKind.String
            && expectedVersion.Equals(version.GetString(), StringComparison.Ordinal);
    }

    private static bool HasProjectLibrary(JsonElement libraries, string key)
    {
        return libraries.TryGetProperty(key, out var value)
            && value.ValueKind == JsonValueKind.Object
            && value.TryGetProperty("type", out var type)
            && type.ValueKind == JsonValueKind.String
            && "project".Equals(type.GetString(), StringComparison.Ordinal);
    }

    private static string SingleProperty(XDocument document, string localName)
    {
        var root = document.Root;
        if (root is null || !root.Name.LocalName.Equals("Project", StringComparison.Ordinal))
        {
            throw new InvalidDataException("SDK bundled versions root must be Project.");
        }

        var properties = root
            .Descendants()
            .Where(element => element.Name.LocalName.Equals(
                localName,
                StringComparison.OrdinalIgnoreCase))
            .Where(element => element.Parent?.Name.LocalName.Equals(
                "PropertyGroup",
                StringComparison.Ordinal) == true)
            .ToArray();
        if (properties.Length != 1)
        {
            throw new InvalidDataException($"Required SDK property '{localName}' is missing or ambiguous.");
        }

        var property = properties[0];
        var group = property.Parent;
        if (group is null
            || group.Parent != root
            || group.Attributes().Any(attribute => attribute.Name.LocalName.Equals(
                "Condition",
                StringComparison.OrdinalIgnoreCase))
            || property.Attribute("Condition") is not null
            || property.HasElements
            || property.HasAttributes
            || string.IsNullOrEmpty(property.Value.Trim()))
        {
            throw new InvalidDataException($"Required SDK property '{localName}' is missing or ambiguous.");
        }

        return property.Value.Trim();
    }

    private static void EnsureClosedSdkEvidenceDocument(XDocument document)
    {
        var root = document.Root;
        if (root is null
            || !root.Name.LocalName.Equals("Project", StringComparison.Ordinal)
            || root.Attributes().Any(attribute => attribute.Name.LocalName.Equals(
                "Sdk",
                StringComparison.OrdinalIgnoreCase))
            || root.Descendants().Any(element =>
                element.Name.LocalName.Equals("Import", StringComparison.OrdinalIgnoreCase)
                || element.Name.LocalName.Equals("Sdk", StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidDataException(
                "SDK bundled versions evidence must not evaluate external imports.");
        }
    }

    private static bool TryParseStableVersion(string value, out Version version)
    {
        var parts = value.Split('.');
        if (parts.Length == 3
            && Version.TryParse(value, out var parsed)
            && parsed.Major.ToString() == parts[0]
            && parsed.Minor.ToString() == parts[1]
            && parsed.Build.ToString() == parts[2])
        {
            version = parsed;
            return true;
        }

        version = new Version();
        return false;
    }

    private sealed record ExportTable(
        string DllName,
        Dictionary<string, ExportEntry> Exports);

    private sealed record ExportEntry(uint FunctionRva, bool IsDirectExecutable);

    private sealed record CanonicalResourceType(
        Win32ResourceIdentifier Identifier,
        CanonicalResourceName[] Names);

    private sealed record CanonicalResourceName(
        Win32ResourceIdentifier Identifier,
        KeyValuePair<Win32ResourceKey, Win32ResourceValue>[] Resources);

    private sealed class CanonicalResourceBuilder
    {
        private readonly List<byte> bytes_ = [];

        public int Count => bytes_.Count;

        public void Align(int alignment)
        {
            if (alignment <= 0 || (alignment & (alignment - 1)) != 0)
            {
                throw new InvalidDataException("Resource alignment is invalid.");
            }

            while ((bytes_.Count & (alignment - 1)) != 0)
            {
                bytes_.Add(0);
            }
        }

        public int ReserveUInt32()
        {
            var offset = bytes_.Count;
            WriteUInt32(0);
            return offset;
        }

        public void PatchUInt32(int offset, uint value)
        {
            if (offset < 0 || offset > bytes_.Count - sizeof(uint))
            {
                throw new InvalidDataException("Resource patch offset is invalid.");
            }

            bytes_[offset] = (byte)(value & 0xff);
            bytes_[offset + 1] = (byte)((value >> 8) & 0xff);
            bytes_[offset + 2] = (byte)((value >> 16) & 0xff);
            bytes_[offset + 3] = (byte)((value >> 24) & 0xff);
        }

        public void WriteUInt16(ushort value)
        {
            bytes_.Add((byte)(value & 0xff));
            bytes_.Add((byte)((value >> 8) & 0xff));
        }

        public void WriteUInt32(uint value)
        {
            bytes_.Add((byte)(value & 0xff));
            bytes_.Add((byte)((value >> 8) & 0xff));
            bytes_.Add((byte)((value >> 16) & 0xff));
            bytes_.Add((byte)((value >> 24) & 0xff));
        }

        public void WriteBytes(ReadOnlySpan<byte> values)
        {
            foreach (var value in values)
            {
                bytes_.Add(value);
            }
        }

        public byte[] ToArray() => bytes_.ToArray();
    }

    private sealed class Win32ResourceReader
    {
        private const uint DirectoryFlag = 0x80000000;
        private const uint OffsetMask = 0x7fffffff;
        private const uint CanonicalCodePage = 1252;

        private readonly byte[] contents_;
        private readonly SectionHeader section_;
        private readonly int rootRva_;
        private readonly int rootFileOffset_;
        private readonly int resourceSize_;
        private readonly HashSet<int> directoryOffsets_ = [];
        private readonly List<ClaimedResourceRange> claimedRanges_ = [];
        private readonly Dictionary<Win32ResourceKey, Win32ResourceValue> resources_ = [];
        private int entryCount_;

        private Win32ResourceReader(
            byte[] contents,
            SectionHeader section,
            int rootRva,
            int rootFileOffset,
            int resourceSize)
        {
            contents_ = contents;
            section_ = section;
            rootRva_ = rootRva;
            rootFileOffset_ = rootFileOffset;
            resourceSize_ = resourceSize;
        }

        public static Dictionary<Win32ResourceKey, Win32ResourceValue> Read(
            byte[] contents,
            PEHeaders headers,
            SectionHeader section)
        {
            var peHeader = headers.PEHeader
                ?? throw new BadImageFormatException("PE resource header is missing.");
            var directory = peHeader.ResourceTableDirectory;
            if (!RvaBelongsToSection(
                    directory.RelativeVirtualAddress,
                    directory.Size,
                    section))
            {
                throw new InvalidDataException(
                    "PE resource directory is outside its resource section.");
            }

            var rootSectionOffset = checked(
                directory.RelativeVirtualAddress - section.VirtualAddress);
            var rootFileOffset = checked(section.PointerToRawData + rootSectionOffset);
            EnsureRange(contents, rootFileOffset, directory.Size);
            var reader = new Win32ResourceReader(
                contents,
                section,
                directory.RelativeVirtualAddress,
                rootFileOffset,
                directory.Size);
            reader.ReadDirectory(
                relativeOffset: 0,
                depth: 0,
                type: default,
                name: default);
            reader.ValidateClaimedRanges();
            return reader.resources_;
        }

        private void ReadDirectory(
            int relativeOffset,
            int depth,
            Win32ResourceIdentifier type,
            Win32ResourceIdentifier name)
        {
            if (depth is < 0 or > 2
                || (relativeOffset & 3) != 0
                || !directoryOffsets_.Add(relativeOffset))
            {
                throw new InvalidDataException("PE resource directory shape is invalid.");
            }

            EnsureResourceRange(relativeOffset, 16);
            var headerOffset = ResourceFileOffset(relativeOffset);
            var characteristics = ReadUInt32(contents_, headerOffset);
            var namedCount = ReadUInt16(contents_, headerOffset + 12);
            var idCount = ReadUInt16(contents_, headerOffset + 14);
            var count = checked((int)namedCount + idCount);
            entryCount_ = checked(entryCount_ + count);
            if (characteristics != 0
                || entryCount_ > MaxResourceEntries)
            {
                throw new InvalidDataException("PE resource directory metadata is invalid.");
            }

            var tableSize = checked(16 + count * 8);
            EnsureResourceRange(relativeOffset, tableSize);
            AddClaim(relativeOffset, tableSize, ResourceRangeKind.Structure);
            string? previousName = null;
            var previousId = -1;
            for (var index = 0; index < count; ++index)
            {
                var entryOffset = checked(headerOffset + 16 + index * 8);
                var rawName = ReadUInt32(contents_, entryOffset);
                var rawTarget = ReadUInt32(contents_, entryOffset + 4);
                var isNamed = (rawName & DirectoryFlag) != 0;
                if (isNamed != index < namedCount)
                {
                    throw new InvalidDataException(
                        "PE resource named and integer entries are not partitioned.");
                }

                var identifier = ReadIdentifier(rawName, isNamed);
                if (isNamed)
                {
                    if (previousName is not null
                        && StringComparer.Ordinal.Compare(previousName, identifier.Name) >= 0)
                    {
                        throw new InvalidDataException(
                            "PE resource names must be strictly ordered.");
                    }

                    previousName = identifier.Name;
                }
                else
                {
                    if (identifier.Id <= previousId)
                    {
                        throw new InvalidDataException(
                            "PE resource identifiers must be strictly ordered.");
                    }

                    previousId = identifier.Id;
                }

                var targetOffset = checked((int)(rawTarget & OffsetMask));
                var targetIsDirectory = (rawTarget & DirectoryFlag) != 0;
                if (depth < 2)
                {
                    if (!targetIsDirectory)
                    {
                        throw new InvalidDataException(
                            "PE resource leaf occurs before the language level.");
                    }

                    ReadDirectory(
                        targetOffset,
                        depth + 1,
                        depth == 0 ? identifier : type,
                        depth == 1 ? identifier : name);
                }
                else
                {
                    if (targetIsDirectory || identifier.Name is not null)
                    {
                        throw new InvalidDataException(
                            "PE resource language must be one integer leaf.");
                    }

                    ReadData(targetOffset, type, name, identifier);
                }
            }
        }

        private Win32ResourceIdentifier ReadIdentifier(uint rawName, bool isNamed)
        {
            if (!isNamed)
            {
                if (rawName > ushort.MaxValue)
                {
                    throw new InvalidDataException("PE resource integer identifier is invalid.");
                }

                return new Win32ResourceIdentifier(null, checked((ushort)rawName));
            }

            var nameOffset = checked((int)(rawName & OffsetMask));
            if ((nameOffset & 1) != 0)
            {
                throw new InvalidDataException("PE resource string offset is invalid.");
            }

            EnsureResourceRange(nameOffset, 2);
            var length = ReadUInt16(contents_, ResourceFileOffset(nameOffset));
            if (length == 0 || length > MaxResourceNameCharacters)
            {
                throw new InvalidDataException("PE resource name length is invalid.");
            }

            var byteLength = checked(length * 2);
            EnsureResourceRange(checked(nameOffset + 2), byteLength);
            AddClaim(
                nameOffset,
                checked(byteLength + 2),
                ResourceRangeKind.Name);
            var value = StrictUtf16LittleEndian.GetString(
                contents_,
                ResourceFileOffset(checked(nameOffset + 2)),
                byteLength);
            if (value.Contains('\0', StringComparison.Ordinal))
            {
                throw new InvalidDataException("PE resource name contains a null character.");
            }

            return new Win32ResourceIdentifier(value, 0);
        }

        private void ReadData(
            int relativeOffset,
            Win32ResourceIdentifier type,
            Win32ResourceIdentifier name,
            Win32ResourceIdentifier language)
        {
            if ((relativeOffset & 3) != 0)
            {
                throw new InvalidDataException("PE resource data entry is not aligned.");
            }

            EnsureResourceRange(relativeOffset, 16);
            AddClaim(relativeOffset, 16, ResourceRangeKind.Structure);
            var dataOffset = ResourceFileOffset(relativeOffset);
            var dataRvaValue = ReadUInt32(contents_, dataOffset);
            var sizeValue = ReadUInt32(contents_, dataOffset + 4);
            var codePage = ReadUInt32(contents_, dataOffset + 8);
            var reserved = ReadUInt32(contents_, dataOffset + 12);
            if (dataRvaValue > int.MaxValue
                || sizeValue is 0 or > int.MaxValue
                || reserved != 0
                || (dataRvaValue & 3) != 0
                || codePage is not 0 and not CanonicalCodePage)
            {
                throw new InvalidDataException("PE resource data entry is invalid.");
            }

            var dataRva = checked((int)dataRvaValue);
            var size = checked((int)sizeValue);
            if (!RvaBelongsToSection(dataRva, size, section_))
            {
                throw new InvalidDataException(
                    "PE resource payload is outside its resource section.");
            }

            var payloadRelativeOffset = checked(dataRva - rootRva_);
            EnsureResourceRange(payloadRelativeOffset, size);
            AddClaim(payloadRelativeOffset, size, ResourceRangeKind.Payload);
            var payloadFileOffset = checked(
                section_.PointerToRawData + dataRva - section_.VirtualAddress);
            EnsureRange(contents_, payloadFileOffset, size);
            var key = new Win32ResourceKey(type, name, language);
            if (!resources_.TryAdd(
                    key,
                    new Win32ResourceValue(
                        payloadFileOffset,
                        size)))
            {
                throw new InvalidDataException("PE resource leaf is duplicated.");
            }
        }

        private void ValidateClaimedRanges()
        {
            var ordered = claimedRanges_
                .OrderBy(range => range.Offset)
                .ThenBy(range => range.Length)
                .ToArray();
            var accepted = new List<ClaimedResourceRange>(ordered.Length);
            foreach (var range in ordered)
            {
                if (accepted.Count != 0)
                {
                    var previous = accepted[^1];
                    if (range.Offset == previous.Offset
                        && range.Length == previous.Length
                        && range.Kind == ResourceRangeKind.Name
                        && previous.Kind == ResourceRangeKind.Name)
                    {
                        continue;
                    }

                    if (range.Offset < checked(previous.Offset + previous.Length))
                    {
                        throw new InvalidDataException(
                            "PE resource structures overlap.");
                    }
                }

                accepted.Add(range);
            }

        }

        private void AddClaim(int offset, int length, ResourceRangeKind kind)
        {
            EnsureResourceRange(offset, length);
            claimedRanges_.Add(new ClaimedResourceRange(offset, length, kind));
        }

        private void EnsureResourceRange(int offset, int length)
        {
            if (offset < 0
                || length <= 0
                || offset > resourceSize_ - length)
            {
                throw new InvalidDataException("PE resource offset is outside its directory.");
            }
        }

        private int ResourceFileOffset(int relativeOffset) =>
            checked(rootFileOffset_ + relativeOffset);
    }

    private enum ResourceRangeKind
    {
        Structure,
        Name,
        Payload,
    }

    private readonly record struct ClaimedResourceRange(
        int Offset,
        int Length,
        ResourceRangeKind Kind);

    private readonly record struct Win32ResourceIdentifier(string? Name, ushort Id);

    private readonly record struct Win32ResourceKey(
        Win32ResourceIdentifier Type,
        Win32ResourceIdentifier Name,
        Win32ResourceIdentifier Language);

    private readonly record struct Win32ResourceValue(
        int Offset,
        int Size);

    private readonly record struct VersionInfoBlock(
        int Offset,
        int End,
        int ValueOffset,
        int ValueByteLength,
        int ChildrenOffset,
        ushort Type,
        string Key);

    private readonly record struct ByteRange(int Offset, int Length);

    private readonly record struct PeLayout(
        int PeSignatureOffset,
        int CoffHeaderOffset,
        int OptionalHeaderOffset,
        int OptionalHeaderSize,
        int SectionHeadersOffset);
}
