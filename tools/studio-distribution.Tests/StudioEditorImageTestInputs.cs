using System.Buffers.Binary;
using System.Diagnostics;
using System.Security;
using System.Text;
using System.Text.Json;
using Xunit;

namespace Asharia.Studio.Distribution.Tests;

[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class StudioEditorImageInputCollection
    : ICollectionFixture<StudioEditorImageTestInputs>
{
    public const string Name = "Studio Editor Image inputs";
}

public sealed class StudioEditorImageTestInputs : IDisposable
{
    internal static readonly string[] EditorNativeRequiredExports =
    [
        "editor_frame_debugger_acquire_snapshot",
        "editor_frame_debugger_release_snapshot",
        "editor_frame_debugger_request_capture",
        "editor_frame_debugger_request_resume",
        "editor_frame_debugger_select_execution_event",
        "editor_viewport_acquire_present_packet",
        "editor_viewport_query_composition_compatibility",
        "editor_viewport_release_compatibility_result",
        "editor_viewport_release_present_packet",
        "editor_viewport_shutdown",
    ];

    internal static readonly string[] SlangRequiredExports =
    [
        "slang_createGlobalSession2",
        "spGetBuildTagString",
    ];

    public StudioEditorImageTestInputs()
    {
        if (!OperatingSystem.IsWindows())
        {
            Root = string.Empty;
            PublishRoot = string.Empty;
            DotnetRoot = string.Empty;
            DotnetHost = string.Empty;
            SdkVersion = string.Empty;
            HostFxrVersion = string.Empty;
            HostRuntimeVersion = string.Empty;
            ReferencePackVersion = string.Empty;
            return;
        }

        Root = Path.Combine(
            Path.GetTempPath(),
            $"asharia-studio-identity-inputs-{Guid.NewGuid():N}");
        PublishRoot = Path.Combine(Root, "publish");
        DotnetRoot = Path.Combine(Root, "dotnet");
        Directory.CreateDirectory(Root);

        DotnetHost = FindDotnetHost();
        var installedDotnetRoot = Path.GetDirectoryName(DotnetHost)!;
        var repositoryRoot = FindRepositoryRoot();
        SdkVersion = QueryPinnedNet10SdkVersion(DotnetHost, repositoryRoot);
        var installedSdkRoot = Path.Combine(installedDotnetRoot, "sdk", SdkVersion);
        HostRuntimeVersion = ReadSdkRuntimeVersion(
            Path.Combine(installedSdkRoot, "dotnet.runtimeconfig.json"));
        HostFxrVersion = HostRuntimeVersion;
        ReferencePackVersion = HostRuntimeVersion;

        CreateMinimalDotnetInput(installedDotnetRoot, installedSdkRoot);
        CreatePublishedEditorFixture(repositoryRoot);
    }

    public string Root { get; }

    public string PublishRoot { get; }

    public string DotnetRoot { get; }

    public string DotnetHost { get; }

    public string SdkVersion { get; }

    public string HostFxrVersion { get; }

    public string HostRuntimeVersion { get; }

    public string ReferencePackVersion { get; }

    internal void CopyPublishTo(string destination) => CopyTree(PublishRoot, destination);

    internal void CopyDotnetTo(string destination) => CopyTree(DotnetRoot, destination);

    public void Dispose()
    {
        if (!string.IsNullOrEmpty(Root) && Directory.Exists(Root))
        {
            Directory.Delete(Root, recursive: true);
        }
    }

    internal static void WriteNativeDll(
        string path,
        string dllName,
        IReadOnlyCollection<string> exports)
    {
        const int peOffset = 0x80;
        const int optionalHeaderSize = 0xf0;
        const int fileAlignment = 0x200;
        const int sectionAlignment = 0x1000;
        const int textRawOffset = 0x200;
        const int textRva = 0x1000;
        const int exportRawOffset = 0x400;
        const int exportRva = 0x2000;
        const int exportRawSize = 0x400;

        var names = exports
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToArray();
        if (names.Length == 0 || names.Any(name => !IsPortableAscii(name)))
        {
            throw new ArgumentException("At least one portable ASCII export is required.", nameof(exports));
        }

        var contents = new byte[exportRawOffset + exportRawSize];
        contents[0] = (byte)'M';
        contents[1] = (byte)'Z';
        WriteUInt32(contents, 0x3c, peOffset);
        contents[peOffset] = (byte)'P';
        contents[peOffset + 1] = (byte)'E';
        var coff = peOffset + 4;
        WriteUInt16(contents, coff, 0x8664);
        WriteUInt16(contents, coff + 2, 2);
        WriteUInt16(contents, coff + 16, optionalHeaderSize);
        WriteUInt16(contents, coff + 18, 0x2022);

        var optional = coff + 20;
        WriteUInt16(contents, optional, 0x20b);
        contents[optional + 2] = 14;
        WriteUInt32(contents, optional + 4, fileAlignment);
        WriteUInt32(contents, optional + 8, exportRawSize);
        WriteUInt32(contents, optional + 20, textRva);
        WriteUInt64(contents, optional + 24, 0x0000000180000000);
        WriteUInt32(contents, optional + 32, sectionAlignment);
        WriteUInt32(contents, optional + 36, fileAlignment);
        WriteUInt16(contents, optional + 40, 6);
        WriteUInt16(contents, optional + 48, 6);
        WriteUInt32(contents, optional + 56, 0x3000);
        WriteUInt32(contents, optional + 60, fileAlignment);
        WriteUInt16(contents, optional + 68, 3);
        WriteUInt16(contents, optional + 70, 0x0160);
        WriteUInt64(contents, optional + 72, 0x100000);
        WriteUInt64(contents, optional + 80, 0x1000);
        WriteUInt64(contents, optional + 88, 0x100000);
        WriteUInt64(contents, optional + 96, 0x1000);
        WriteUInt32(contents, optional + 108, 16);

        var sectionHeaders = optional + optionalHeaderSize;
        WriteSectionHeader(
            contents,
            sectionHeaders,
            ".text",
            1,
            textRva,
            fileAlignment,
            textRawOffset,
            0x60000020);
        WriteSectionHeader(
            contents,
            sectionHeaders + 40,
            ".edata",
            exportRawSize,
            exportRva,
            exportRawSize,
            exportRawOffset,
            0x40000040);
        contents[textRawOffset] = 0xc3;

        var cursor = 40;
        var functionsOffset = cursor;
        cursor += checked(names.Length * sizeof(uint));
        var namesOffset = cursor;
        cursor += checked(names.Length * sizeof(uint));
        var ordinalsOffset = cursor;
        cursor += checked(names.Length * sizeof(ushort));
        var dllNameOffset = cursor;
        cursor = WriteAscii(contents, exportRawOffset + cursor, dllName) - exportRawOffset;
        var exportNameOffsets = new int[names.Length];
        for (var index = 0; index < names.Length; ++index)
        {
            exportNameOffsets[index] = cursor;
            cursor = WriteAscii(contents, exportRawOffset + cursor, names[index]) - exportRawOffset;
        }

        if (cursor > exportRawSize)
        {
            throw new InvalidOperationException("Synthetic export table exceeds its bounded section.");
        }

        var exportDirectory = exportRawOffset;
        WriteUInt32(contents, exportDirectory + 12, exportRva + dllNameOffset);
        WriteUInt32(contents, exportDirectory + 16, 1);
        WriteUInt32(contents, exportDirectory + 20, names.Length);
        WriteUInt32(contents, exportDirectory + 24, names.Length);
        WriteUInt32(contents, exportDirectory + 28, exportRva + functionsOffset);
        WriteUInt32(contents, exportDirectory + 32, exportRva + namesOffset);
        WriteUInt32(contents, exportDirectory + 36, exportRva + ordinalsOffset);
        for (var index = 0; index < names.Length; ++index)
        {
            WriteUInt32(contents, exportRawOffset + functionsOffset + index * 4, textRva);
            WriteUInt32(
                contents,
                exportRawOffset + namesOffset + index * 4,
                exportRva + exportNameOffsets[index]);
            WriteUInt16(contents, exportRawOffset + ordinalsOffset + index * 2, index);
        }

        WriteUInt32(contents, optional + 112, exportRva);
        WriteUInt32(contents, optional + 116, cursor);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, contents);
    }

    private void CreatePublishedEditorFixture(string repositoryRoot)
    {
        var projectRoot = Path.Combine(Root, "editor-project");
        Directory.CreateDirectory(projectRoot);
        var globalJson = $$"""
            {
              "sdk": {
                "version": "{{SdkVersion}}",
                "rollForward": "disable",
                "allowPrerelease": false
              }
            }
            """;
        File.WriteAllText(
            Path.Combine(projectRoot, "global.json"),
            globalJson + "\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        var runtimeContracts = Path.Combine(
            repositoryRoot,
            "apps",
            "studio",
            "src",
            "Asharia.Runtime.Contracts",
            "Asharia.Runtime.Contracts.csproj");
        var editorContracts = Path.Combine(
            repositoryRoot,
            "apps",
            "studio",
            "src",
            "Asharia.Editor",
            "Asharia.Editor.csproj");
        var project = $$"""
            <Project Sdk="Microsoft.NET.Sdk">
              <PropertyGroup>
                <OutputType>WinExe</OutputType>
                <TargetFramework>net10.0</TargetFramework>
                <RuntimeIdentifier>win-x64</RuntimeIdentifier>
                <SelfContained>false</SelfContained>
                <UseAppHost>true</UseAppHost>
                <PublishSingleFile>false</PublishSingleFile>
                <AssemblyName>Editor</AssemblyName>
                <AppHostDotNetSearch>AppRelative</AppHostDotNetSearch>
                <AppHostRelativeDotNet>../managed/dotnet</AppHostRelativeDotNet>
              </PropertyGroup>
              <ItemGroup>
                <ProjectReference Include="{{SecurityElement.Escape(runtimeContracts)}}" />
                <ProjectReference Include="{{SecurityElement.Escape(editorContracts)}}" />
              </ItemGroup>
            </Project>
            """;
        File.WriteAllText(
            Path.Combine(projectRoot, "Editor.csproj"),
            project,
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        File.WriteAllText(
            Path.Combine(projectRoot, "Program.cs"),
            "using Asharia.Editor.Selection;\nusing Asharia.Runtime;\n"
                + "_ = typeof(IEditorSelectionService);\n_ = typeof(EntityId);\n",
            new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));

        Run(
            DotnetHost,
            projectRoot,
            TimeSpan.FromMinutes(2),
            "publish",
            "--disable-build-servers",
            Path.Combine(projectRoot, "Editor.csproj"),
            "-c",
            "Release",
            "-o",
            PublishRoot);
        WriteNativeDll(
            Path.Combine(PublishRoot, "editor_native.dll"),
            "editor_native.dll",
            EditorNativeRequiredExports);
        WriteNativeDll(
            Path.Combine(PublishRoot, "slang.dll"),
            "slang.dll",
            SlangRequiredExports);
    }

    private void CreateMinimalDotnetInput(
        string installedDotnetRoot,
        string installedSdkRoot)
    {
        CopyInput(DotnetHost, "dotnet.exe");
        CopyInput(
            Path.Combine(installedSdkRoot, "dotnet.dll"),
            $"sdk/{SdkVersion}/dotnet.dll");
        CopyInput(
            Path.Combine(installedSdkRoot, "Microsoft.NETCoreSdk.BundledVersions.props"),
            $"sdk/{SdkVersion}/Microsoft.NETCoreSdk.BundledVersions.props");
        CopyInput(
            Path.Combine(installedSdkRoot, "dotnet.runtimeconfig.json"),
            $"sdk/{SdkVersion}/dotnet.runtimeconfig.json");
        CopyInput(
            Path.Combine(installedSdkRoot, "AppHostTemplate", "apphost.exe"),
            $"sdk/{SdkVersion}/AppHostTemplate/apphost.exe");
        CopyInput(
            Path.Combine(installedDotnetRoot, "host", "fxr", HostFxrVersion, "hostfxr.dll"),
            $"host/fxr/{HostFxrVersion}/hostfxr.dll");
        CopyInput(
            Path.Combine(
                installedDotnetRoot,
                "shared",
                "Microsoft.NETCore.App",
                HostRuntimeVersion,
                "System.Private.CoreLib.dll"),
            $"shared/Microsoft.NETCore.App/{HostRuntimeVersion}/System.Private.CoreLib.dll");
        CopyInput(
            Path.Combine(
                installedDotnetRoot,
                "packs",
                "Microsoft.NETCore.App.Ref",
                ReferencePackVersion,
                "ref",
                "net10.0",
                "System.Runtime.dll"),
            $"packs/Microsoft.NETCore.App.Ref/{ReferencePackVersion}/ref/net10.0/System.Runtime.dll");
    }

    private void CopyInput(string source, string relativeDestination)
    {
        if (!File.Exists(source))
        {
            throw new FileNotFoundException(
                "Required installed .NET 10 identity input was not found.",
                source);
        }

        var destination = Path.Combine(
            DotnetRoot,
            relativeDestination.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        File.Copy(source, destination);
    }

    private static void CopyTree(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (var directory in Directory.EnumerateDirectories(
                     source,
                     "*",
                     SearchOption.AllDirectories))
        {
            Directory.CreateDirectory(Path.Combine(destination, Path.GetRelativePath(source, directory)));
        }

        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            var target = Path.Combine(destination, Path.GetRelativePath(source, file));
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(file, target);
        }
    }

    private static string ReadSdkRuntimeVersion(string runtimeConfigPath)
    {
        using var document = JsonDocument.Parse(File.ReadAllBytes(runtimeConfigPath));
        return document.RootElement
            .GetProperty("runtimeOptions")
            .GetProperty("framework")
            .GetProperty("version")
            .GetString()
            ?? throw new InvalidDataException("SDK runtimeconfig framework version is missing.");
    }

    private static string QueryPinnedNet10SdkVersion(
        string dotnetHost,
        string repositoryRoot)
    {
        var value = Run(
                dotnetHost,
                repositoryRoot,
                TimeSpan.FromSeconds(30),
                "--version")
            .Trim();
        if (value.Contains('-', StringComparison.Ordinal)
            || !Version.TryParse(value, out var parsed)
            || parsed is not
            {
                Major: 10,
                Minor: 0,
                Build: >= 0,
            })
        {
            throw new InvalidOperationException(
                "Repository global.json must select one stable .NET 10.0 SDK.");
        }

        return value;
    }

    private static string Run(
        string executable,
        string workingDirectory,
        TimeSpan timeout,
        params string[] arguments)
    {
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = executable,
                WorkingDirectory = workingDirectory,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            },
        };
        foreach (var argument in arguments)
        {
            process.StartInfo.ArgumentList.Add(argument);
        }

        if (!process.Start())
        {
            throw new InvalidOperationException($"Could not start '{executable}'.");
        }

        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        if (!process.WaitForExit(checked((int)timeout.TotalMilliseconds)))
        {
            process.Kill(entireProcessTree: true);
            process.WaitForExit();
            throw new TimeoutException($"'{executable}' did not finish within {timeout}.");
        }

        var output = stdout.GetAwaiter().GetResult();
        var error = stderr.GetAwaiter().GetResult();
        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException(
                $"'{executable}' exited with {process.ExitCode}:{Environment.NewLine}{output}{Environment.NewLine}{error}");
        }

        return output;
    }

    private static string FindDotnetHost()
    {
        var explicitHost = Environment.GetEnvironmentVariable("DOTNET_HOST_PATH");
        if (!string.IsNullOrWhiteSpace(explicitHost) && File.Exists(explicitHost))
        {
            return explicitHost;
        }

        var runtimeDirectory = new FileInfo(typeof(object).Assembly.Location).Directory!;
        var dotnetRoot = runtimeDirectory.Parent!.Parent!.Parent!;
        var candidate = Path.Combine(dotnetRoot.FullName, "dotnet.exe");
        if (!File.Exists(candidate))
        {
            throw new FileNotFoundException("Could not locate the active dotnet host.", candidate);
        }

        return candidate;
    }

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "CMakeLists.txt"))
                && File.Exists(Path.Combine(current.FullName, "apps", "studio", "Editor.csproj")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the Asharia repository root.");
    }

    private static bool IsPortableAscii(string value) =>
        !string.IsNullOrEmpty(value) && value.All(character => character is >= '!' and <= '~');

    private static void WriteSectionHeader(
        byte[] contents,
        int offset,
        string name,
        int virtualSize,
        int virtualAddress,
        int rawSize,
        int rawOffset,
        uint characteristics)
    {
        Encoding.ASCII.GetBytes(name).CopyTo(contents, offset);
        WriteUInt32(contents, offset + 8, virtualSize);
        WriteUInt32(contents, offset + 12, virtualAddress);
        WriteUInt32(contents, offset + 16, rawSize);
        WriteUInt32(contents, offset + 20, rawOffset);
        WriteUInt32(contents, offset + 36, characteristics);
    }

    private static int WriteAscii(byte[] contents, int offset, string value)
    {
        var bytes = Encoding.ASCII.GetBytes(value);
        bytes.CopyTo(contents, offset);
        contents[offset + bytes.Length] = 0;
        return offset + bytes.Length + 1;
    }

    private static void WriteUInt16(byte[] contents, int offset, int value) =>
        BinaryPrimitives.WriteUInt16LittleEndian(contents.AsSpan(offset, 2), checked((ushort)value));

    private static void WriteUInt32(byte[] contents, int offset, int value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(contents.AsSpan(offset, 4), checked((uint)value));

    private static void WriteUInt32(byte[] contents, int offset, uint value) =>
        BinaryPrimitives.WriteUInt32LittleEndian(contents.AsSpan(offset, 4), value);

    private static void WriteUInt64(byte[] contents, int offset, long value) =>
        BinaryPrimitives.WriteUInt64LittleEndian(contents.AsSpan(offset, 8), checked((ulong)value));
}
