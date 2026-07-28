using System.Text;
using System.Text.Json;

namespace Asharia.Studio.Distribution;

internal static class ManagedBuildEnvironmentMetadataWriter
{
    public const string RelativePath = "metadata/managed-build-environment.json";

    public static byte[] Write(
        string environmentId,
        string targetFramework,
        string dotnetHostName,
        string sdkVersion,
        string hostFxrVersion,
        string hostRuntimeVersion,
        string referencePackVersion,
        string runtimeContractPath,
        string editorContractPath)
    {
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(
            stream,
            new JsonWriterOptions
            {
                Indented = true,
            }))
        {
            const string dotnetRoot = "managed/dotnet";
            var sdkRoot = $"{dotnetRoot}/sdk/{sdkVersion}";
            var hostFxrRoot = $"{dotnetRoot}/host/fxr/{hostFxrVersion}";
            var hostRuntimeRoot =
                $"{dotnetRoot}/shared/Microsoft.NETCore.App/{hostRuntimeVersion}";
            var referencePackRoot =
                $"{dotnetRoot}/packs/Microsoft.NETCore.App.Ref/{referencePackVersion}";

            writer.WriteStartObject();
            writer.WriteString("schema", "com.asharia.managed-build-environment");
            writer.WriteNumber("schemaVersion", 1);
            writer.WriteString("environmentId", environmentId);
            writer.WriteString("targetFramework", targetFramework);
            writer.WriteString("dotnetRoot", dotnetRoot);
            writer.WriteString("dotnetHostPath", $"{dotnetRoot}/{dotnetHostName}");

            writer.WriteStartObject("sdk");
            writer.WriteString("version", sdkVersion);
            writer.WriteString("root", sdkRoot);
            writer.WriteString("entryPath", $"{sdkRoot}/dotnet.dll");
            writer.WriteString(
                "bundledVersionsPath",
                $"{sdkRoot}/Microsoft.NETCoreSdk.BundledVersions.props");
            writer.WriteString("runtimeConfigPath", $"{sdkRoot}/dotnet.runtimeconfig.json");
            writer.WriteEndObject();

            writer.WriteStartObject("hostFxr");
            writer.WriteString("version", hostFxrVersion);
            writer.WriteString("root", hostFxrRoot);
            writer.WriteEndObject();

            writer.WriteStartObject("hostRuntime");
            writer.WriteString("version", hostRuntimeVersion);
            writer.WriteString("root", hostRuntimeRoot);
            writer.WriteEndObject();

            writer.WriteStartObject("referencePack");
            writer.WriteString("name", "Microsoft.NETCore.App.Ref");
            writer.WriteString("version", referencePackVersion);
            writer.WriteString("root", referencePackRoot);
            writer.WriteString("assembliesRoot", $"{referencePackRoot}/ref/{targetFramework}");
            writer.WriteEndObject();

            writer.WriteStartObject("contracts");
            writer.WriteString("runtimePath", runtimeContractPath);
            writer.WriteString("editorPath", editorContractPath);
            writer.WriteEndObject();
            writer.WriteEndObject();
        }

        var rendered = stream.ToArray();
        using var normalized = new MemoryStream(rendered.Length + 1);
        for (var index = 0; index < rendered.Length; ++index)
        {
            if (rendered[index] == (byte)'\r'
                && index + 1 < rendered.Length
                && rendered[index + 1] == (byte)'\n')
            {
                continue;
            }

            normalized.WriteByte(rendered[index]);
        }

        normalized.WriteByte((byte)'\n');
        return normalized.ToArray();
    }
}
