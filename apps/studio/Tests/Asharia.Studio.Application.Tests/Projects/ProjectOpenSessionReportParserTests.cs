using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using Asharia.Editor.Projects;
using Asharia.Studio.Application.Projects;
using Xunit;

namespace Asharia.Studio.Application.Tests.Projects;

public sealed class ProjectOpenSessionReportParserTests
{
    [Fact]
    public void Parse_consumes_the_python_canonical_ready_fixture()
    {
        var result = ProjectOpenSessionReportParser.Parse(ReadyReportBytes());

        Assert.True(result.Succeeded, Render(result));
        Assert.Null(result.Error);
        var snapshot = Assert.IsType<ProjectOpenSessionSnapshot>(
            result.Snapshot);
        Assert.Equal(ProjectOpenSessionState.Ready, snapshot.State);
        Assert.Equal(
            ProjectOpenNextAction.ActivateProjectProfile,
            snapshot.NextAction);
        Assert.True(snapshot.IsBootstrapReady);
        var project = Assert.IsType<ProjectOpenSummarySnapshot>(
            snapshot.Project);
        Assert.Equal("Example", project.ProjectName);
        Assert.Equal(
            Guid.Parse("6ad468bb-e099-46d4-a91b-911e86cf7188"),
            project.ProjectId);
        Assert.Equal(1UL, project.AssetSourceRootCount);
        Assert.Empty(snapshot.Diagnostics);
    }

    [Theory]
    [InlineData("NoProject", "SelectProject")]
    [InlineData("Opening", "InspectProject")]
    [InlineData("PendingBuild", "BuildProjectHost")]
    [InlineData("PendingRestart", "RestartEditor")]
    [InlineData("RepairRequired", "RepairDistribution")]
    [InlineData("UpgradeRequired", "UpgradeEngine")]
    [InlineData("SafeMode", "OpenSafeMode")]
    [InlineData("FatalDistributionError", "RepairEditorImage")]
    public void Parse_preserves_the_exact_non_ready_state_action_mapping(
        string state,
        string nextAction)
    {
        var report = ReadyReport()
            .Replace(
                "\"state\": \"Ready\"",
                $"\"state\": \"{state}\"",
                StringComparison.Ordinal)
            .Replace(
                "\"nextAction\": \"ActivateProjectProfile\"",
                $"\"nextAction\": \"{nextAction}\"",
                StringComparison.Ordinal)
            .Replace(
                ProjectBlock,
                "  \"project\": null",
                StringComparison.Ordinal);

        var result = ProjectOpenSessionReportParser.Parse(
            Encoding.UTF8.GetBytes(report));

        Assert.True(result.Succeeded, Render(result));
        Assert.False(result.Snapshot!.IsBootstrapReady);
        Assert.Null(result.Snapshot.Project);
    }

    [Fact]
    public void Parse_preserves_canonical_diagnostics()
    {
        var bytes = Replace(
            ReadyReport(),
            "\"diagnostics\": []",
            "\"diagnostics\": [\n"
                + "    {\n"
                + "      \"code\": \"bootstrap.test\",\n"
                + "      \"manifestPath\": \"asharia.bootstrap-session.json\",\n"
                + "      \"pointer\": \"/state\",\n"
                + "      \"message\": \"Test diagnostic.\"\n"
                + "    }\n"
                + "  ]");

        var result = ProjectOpenSessionReportParser.Parse(bytes);

        Assert.True(result.Succeeded, Render(result));
        var diagnostic = Assert.Single(result.Snapshot!.Diagnostics);
        Assert.Equal("bootstrap.test", diagnostic.Code);
        Assert.Equal(
            "asharia.bootstrap-session.json",
            diagnostic.ManifestPath);
        Assert.Equal("/state", diagnostic.Pointer);
        Assert.Equal("Test diagnostic.", diagnostic.Message);
    }

    [Fact]
    public void Parse_uses_the_python_utf8_diagnostic_order()
    {
        var bytes = Replace(
            ReadyReport(),
            "\"diagnostics\": []",
            "\"diagnostics\": [\n"
                + "    {\n"
                + "      \"code\": \"bootstrap.test\",\n"
                + "      \"manifestPath\": \"\uE000.json\",\n"
                + "      \"pointer\": \"/state\",\n"
                + "      \"message\": \"First.\"\n"
                + "    },\n"
                + "    {\n"
                + "      \"code\": \"bootstrap.test\",\n"
                + "      \"manifestPath\": \"\U00010000.json\",\n"
                + "      \"pointer\": \"/state\",\n"
                + "      \"message\": \"Second.\"\n"
                + "    }\n"
                + "  ]");

        var result = ProjectOpenSessionReportParser.Parse(bytes);

        Assert.True(result.Succeeded, Render(result));
        Assert.Equal(
            ["\uE000.json", "\U00010000.json"],
            result.Snapshot!.Diagnostics
                .Select(diagnostic => diagnostic.ManifestPath));
    }

    [Fact]
    public void Parse_preserves_the_unsigned_project_asset_root_count()
    {
        var bytes = Replace(
            ReadyReport(),
            "\"assetSourceRootCount\": 1",
            "\"assetSourceRootCount\": 18446744073709551615");

        var result = ProjectOpenSessionReportParser.Parse(bytes);

        Assert.True(result.Succeeded, Render(result));
        Assert.Equal(
            ulong.MaxValue,
            result.Snapshot!.Project!.AssetSourceRootCount);
    }

    [Theory]
    [InlineData("bom")]
    [InlineData("crlf")]
    [InlineData("missing-newline")]
    [InlineData("schema")]
    [InlineData("version")]
    [InlineData("unknown-field")]
    [InlineData("missing-field")]
    [InlineData("reordered")]
    [InlineData("state")]
    [InlineData("next-action")]
    [InlineData("state-action")]
    [InlineData("ready-without-project")]
    [InlineData("non-ready-with-project")]
    [InlineData("project-id")]
    [InlineData("asset-root-count")]
    [InlineData("integrity")]
    [InlineData("absolute-path")]
    [InlineData("diagnostic-order")]
    public void Parse_rejects_noncanonical_or_invalid_reports(string mutation)
    {
        var bytes = Mutate(mutation);

        var result = ProjectOpenSessionReportParser.Parse(bytes);

        Assert.False(result.Succeeded);
        Assert.Null(result.Snapshot);
        Assert.NotNull(result.Error);
        Assert.False(string.IsNullOrWhiteSpace(result.Error.Message));
    }

    [Fact]
    public void Parse_returns_typed_failure_instead_of_exposing_json_exception()
    {
        var result = ProjectOpenSessionReportParser.Parse(
            Encoding.UTF8.GetBytes("{invalid}\n"));

        Assert.False(result.Succeeded);
        Assert.Equal(
            ProjectOpenSessionReportParseErrorCode.InvalidJson,
            result.Error?.Code);
        Assert.Equal(string.Empty, result.Error?.Pointer);
    }

    private const string ProjectBlock =
        "  \"project\": {\n"
        + "    \"projectName\": \"Example\",\n"
        + "    \"projectId\": \"6ad468bb-e099-46d4-a91b-911e86cf7188\",\n"
        + "    \"assetSourceRootCount\": 1\n"
        + "  }";

    private static byte[] ReadyReportBytes() =>
        Encoding.UTF8.GetBytes(ReadyReport());

    private static string ReadyReport()
    {
        var assembly = typeof(ProjectOpenSessionReportParserTests).Assembly;
        var resourceName = assembly.GetManifestResourceNames()
            .Single(name => name.EndsWith(
                "bootstrap-session-ready-v1.json",
                StringComparison.Ordinal));
        using var stream = assembly.GetManifestResourceStream(resourceName)
            ?? throw new InvalidOperationException(
                "Ready project-open fixture is missing.");
        using var reader = new StreamReader(
            stream,
            Encoding.UTF8,
            detectEncodingFromByteOrderMarks: false);
        return reader.ReadToEnd();
    }

    private static byte[] Mutate(string mutation)
    {
        var report = ReadyReport();
        return mutation switch
        {
            "bom" => [0xef, 0xbb, 0xbf, .. Encoding.UTF8.GetBytes(report)],
            "crlf" => Encoding.UTF8.GetBytes(
                report.Replace("\n", "\r\n", StringComparison.Ordinal)),
            "missing-newline" => Encoding.UTF8.GetBytes(
                report.TrimEnd('\n')),
            "schema" => Replace(
                report,
                "com.asharia.bootstrap-session",
                "com.asharia.bootstrap-session-next"),
            "version" => Replace(report, "\"schemaVersion\": 1", "\"schemaVersion\": 2"),
            "unknown-field" => Replace(
                report,
                "  \"schema\":",
                "  \"unknown\": null,\n  \"schema\":"),
            "missing-field" => Replace(
                report,
                "  \"currentSessionIntegrity\": {\n"
                    + "    \"algorithm\": \"sha256\",\n"
                    + "    \"digest\": \"3333333333333333333333333333333333333333333333333333333333333333\"\n"
                    + "  },\n",
                string.Empty),
            "reordered" => Replace(
                report,
                "  \"state\": \"Ready\",\n"
                    + "  \"nextAction\": \"ActivateProjectProfile\",\n",
                "  \"nextAction\": \"ActivateProjectProfile\",\n"
                    + "  \"state\": \"Ready\",\n"),
            "state" => Replace(report, "\"state\": \"Ready\"", "\"state\": \"Unknown\""),
            "next-action" => Replace(
                report,
                "\"nextAction\": \"ActivateProjectProfile\"",
                "\"nextAction\": \"Unknown\""),
            "state-action" => Replace(
                report,
                "\"nextAction\": \"ActivateProjectProfile\"",
                "\"nextAction\": \"SelectProject\""),
            "ready-without-project" => Replace(
                report,
                ProjectBlock,
                "  \"project\": null"),
            "non-ready-with-project" => Replace(
                report.Replace(
                    "\"state\": \"Ready\"",
                    "\"state\": \"SafeMode\"",
                    StringComparison.Ordinal),
                "\"nextAction\": \"ActivateProjectProfile\"",
                "\"nextAction\": \"OpenSafeMode\""),
            "project-id" => Replace(
                report,
                "6ad468bb-e099-46d4-a91b-911e86cf7188",
                "6AD468BB-E099-46D4-A91B-911E86CF7188"),
            "asset-root-count" => Replace(
                report,
                "\"assetSourceRootCount\": 1",
                "\"assetSourceRootCount\": -1"),
            "integrity" => Replace(
                report,
                "\"digest\": \"3333333333333333333333333333333333333333333333333333333333333333\"",
                "\"digest\": \"invalid\""),
            "absolute-path" => Replace(
                report,
                "\"diagnostics\": []",
                "\"diagnostics\": [\n"
                    + "    {\n"
                    + "      \"code\": \"bootstrap.test\",\n"
                    + "      \"manifestPath\": \"C:/private/report.json\",\n"
                    + "      \"pointer\": \"/state\",\n"
                    + "      \"message\": \"Invalid state.\"\n"
                    + "    }\n"
                    + "  ]"),
            "diagnostic-order" => Replace(
                report,
                "\"diagnostics\": []",
                "\"diagnostics\": [\n"
                    + "    {\n"
                    + "      \"code\": \"test.z\",\n"
                    + "      \"manifestPath\": \"z.json\",\n"
                    + "      \"pointer\": \"/z\",\n"
                    + "      \"message\": \"Z.\"\n"
                    + "    },\n"
                    + "    {\n"
                    + "      \"code\": \"test.a\",\n"
                    + "      \"manifestPath\": \"a.json\",\n"
                    + "      \"pointer\": \"/a\",\n"
                    + "      \"message\": \"A.\"\n"
                    + "    }\n"
                    + "  ]"),
            _ => throw new ArgumentOutOfRangeException(
                nameof(mutation),
                mutation,
                null),
        };
    }

    private static byte[] Replace(
        string source,
        string oldValue,
        string newValue) =>
        Encoding.UTF8.GetBytes(
            source.Replace(
                oldValue,
                newValue,
                StringComparison.Ordinal));

    private static string Render(
        ProjectOpenSessionReportParseResult result) =>
        result.Error is null
            ? "No parse error."
            : $"{result.Error.Code}: {result.Error.Pointer}: {result.Error.Message}";
}
