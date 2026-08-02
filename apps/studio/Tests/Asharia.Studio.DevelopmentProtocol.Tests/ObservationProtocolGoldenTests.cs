using System;
using System.Collections.Immutable;
using System.Linq;
using System.Text;
using Asharia.Studio.DevelopmentProtocol;
using Xunit;

namespace Asharia.Studio.DevelopmentProtocol.Tests;

public sealed class ObservationProtocolGoldenTests
{
    private static readonly ObservationRequestId RequestId =
        new(Guid.Parse("11111111-2222-3333-4444-555555555555"));
    private static readonly StudioInstanceId InstanceId =
        new(Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
    private static readonly StudioSessionId SessionId =
        new(Guid.Parse("01234567-89ab-cdef-0123-456789abcdef"));

    [Fact]
    public void Session_describe_request_matches_v1_golden_json()
    {
        var request = Request();

        var json = Encoding.UTF8.GetString(ObservationProtocolJson.WriteRequest(request));

        Assert.Equal(
            "{\"protocol\":{\"major\":1,\"minor\":0},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"method\":\"session.describe\",\"timeoutMilliseconds\":1000,\"parameters\":{}}",
            json);
    }

    [Fact]
    public void Pipe_handshake_request_matches_v1_golden_json_and_never_enters_a_response()
    {
        const string attachToken = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
        var request = new ObservationHandshakeRequest(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            attachToken);

        var bytes = ObservationProtocolJson.WriteHandshakeRequest(request);
        var json = Encoding.UTF8.GetString(bytes);
        var parsed = ObservationProtocolJson.ReadHandshakeRequest(bytes);

        Assert.Equal(
            "{\"protocol\":{\"major\":1,\"minor\":0},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"attachToken\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\"}",
            json);
        Assert.True(parsed.Succeeded);
        Assert.Equal(attachToken, parsed.Value!.AttachToken);

        var response = new ObservationResponse<ToolSessionDescriptor>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationOutcome.Failed,
            Value: null,
            new ObservationFailure(
                "observation.handshake.denied",
                "security",
                "Handshake denied.",
                Retryable: false));
        Assert.DoesNotContain(
            attachToken,
            Encoding.UTF8.GetString(ObservationProtocolJson.WriteResponse(response)),
            StringComparison.Ordinal);
    }

    [Fact]
    public void Pipe_handshake_invalid_identity_version_or_token_fails_closed()
    {
        var valid = new ObservationHandshakeRequest(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
        var incompatible = valid with
        {
            Protocol = new ObservationProtocolVersion(Major: 2, Minor: 0),
        };
        var emptyToken = valid with { AttachToken = string.Empty };
        var oversizedToken = valid with
        {
            AttachToken = new string('A', ObservationProtocolLimits.MaxAttachTokenCharacters + 1),
        };

        Assert.Equal(
            "observation.protocol.unsupported",
            ObservationProtocolJson.ReadHandshakeRequest(
                SerializeUnchecked(incompatible)).Failure!.Code);
        Assert.Equal(
            "observation.handshake.invalid",
            ObservationProtocolJson.ReadHandshakeRequest(
                SerializeUnchecked(emptyToken)).Failure!.Code);
        Assert.Throws<ArgumentException>(
            () => ObservationProtocolJson.WriteHandshakeRequest(oversizedToken));
    }

    [Fact]
    public void Session_manifest_matches_v1_golden_json_and_roundtrips()
    {
        var manifest = Manifest();

        var bytes = ObservationProtocolJson.WriteSessionManifest(manifest);
        var json = Encoding.UTF8.GetString(bytes);
        var parsed = ObservationProtocolJson.ReadSessionManifest(bytes);

        Assert.Equal(
            "{\"schemaVersion\":1,\"protocol\":{\"major\":1,\"minor\":0},\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"studioSessionId\":\"01234567-89ab-cdef-0123-456789abcdef\",\"processId\":4242,\"processStartTimeUtc\":\"2026-08-01T01:02:03+00:00\",\"endpointGeneration\":7,\"pipeName\":\"asharia_studio_aaaaaaaabbbbccccddddeeeeeeeeeeee_7\",\"attachToken\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\",\"buildIdentity\":\"asharia-dev-abc123\",\"configuration\":\"Development\",\"capabilityDigest\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"createdAtUtc\":\"2026-08-01T01:02:04+00:00\",\"heartbeatUtc\":\"2026-08-01T01:02:04+00:00\"}",
            json);
        Assert.True(parsed.Succeeded);
        Assert.Equal(manifest, parsed.Value);
    }

    [Fact]
    public void Session_manifest_fails_closed_for_version_encoding_and_size_bounds()
    {
        var unsupported = Manifest() with
        {
            Protocol = new ObservationProtocolVersion(Major: 2, Minor: 0),
        };
        var nonCanonicalDigest = Manifest() with
        {
            CapabilityDigest = new string('a', 64),
        };
        var invalidToken = Manifest() with { AttachToken = new string('A', 44) };
        var oversized = new byte[ObservationProtocolLimits.MaxSessionManifestBytes + 1];

        Assert.Equal(
            "observation.manifest.unsupported",
            ObservationProtocolJson.ReadSessionManifest(
                SerializeUnchecked(unsupported)).Failure!.Code);
        Assert.Equal(
            "observation.manifest.invalid",
            ObservationProtocolJson.ReadSessionManifest(
                SerializeUnchecked(nonCanonicalDigest)).Failure!.Code);
        Assert.Equal(
            "observation.manifest.invalid",
            ObservationProtocolJson.ReadSessionManifest(
                SerializeUnchecked(invalidToken)).Failure!.Code);
        Assert.Equal(
            "observation.manifest.too-large",
            ObservationProtocolJson.ReadSessionManifest(oversized).Failure!.Code);
    }

    [Fact]
    public void Session_manifest_null_required_text_returns_typed_failure()
    {
        var json = Encoding.UTF8.GetBytes(
            "{\"schemaVersion\":1,\"protocol\":{\"major\":1,\"minor\":0},\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"studioSessionId\":\"01234567-89ab-cdef-0123-456789abcdef\",\"processId\":4242,\"processStartTimeUtc\":\"2026-08-01T01:02:03+00:00\",\"endpointGeneration\":7,\"pipeName\":\"pipe\",\"attachToken\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\",\"buildIdentity\":\"build\",\"configuration\":\"Development\",\"capabilityDigest\":null,\"createdAtUtc\":\"2026-08-01T01:02:04+00:00\",\"heartbeatUtc\":\"2026-08-01T01:02:04+00:00\"}");

        var result = ObservationProtocolJson.ReadSessionManifest(json);

        Assert.Equal("observation.manifest.invalid", result.Failure!.Code);
    }

    [Fact]
    public void Session_describe_complete_response_matches_v1_golden_json()
    {
        var response = new ObservationResponse<ToolSessionDescriptor>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationOutcome.Complete,
            new ToolSessionDescriptor(
                InstanceId,
                SessionId,
                ProcessId: 4242,
                ProcessStartTimeUtc: DateTimeOffset.Parse("2026-08-01T01:02:03Z"),
                BuildIdentity: "asharia-dev-abc123",
                Configuration: "Development",
                ObservationProtocolVersion.Current,
                EndpointGeneration: 7,
                State: "ready",
                StartedAtUtc: DateTimeOffset.Parse("2026-08-01T01:02:04Z"),
                UptimeMilliseconds: 1500,
                Capabilities:
                [
                    new ObservationCapabilityDescriptor(
                        "session.core",
                        SchemaVersion: 1,
                        Access: "observe",
                        Cost: "low",
                        Availability: "available",
                        OwnerScopeKind: "process",
                        ProviderGeneration: 1,
                        new ObservationCapabilityBounds(
                            ObservationProtocolLimits.MaxPageSize,
                            ObservationProtocolLimits.MaxResponseBytes,
                            ObservationProtocolLimits.MaxWaitMilliseconds)),
                ]));

        var json = Encoding.UTF8.GetString(ObservationProtocolJson.WriteResponse(response));

        Assert.Equal(
            "{\"protocol\":{\"major\":1,\"minor\":0},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"outcome\":\"complete\",\"value\":{\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"studioSessionId\":\"01234567-89ab-cdef-0123-456789abcdef\",\"processId\":4242,\"processStartTimeUtc\":\"2026-08-01T01:02:03+00:00\",\"buildIdentity\":\"asharia-dev-abc123\",\"configuration\":\"Development\",\"protocol\":{\"major\":1,\"minor\":0},\"endpointGeneration\":7,\"state\":\"ready\",\"startedAtUtc\":\"2026-08-01T01:02:04+00:00\",\"uptimeMilliseconds\":1500,\"capabilities\":[{\"capabilityId\":\"session.core\",\"schemaVersion\":1,\"access\":\"observe\",\"cost\":\"low\",\"availability\":\"available\",\"ownerScopeKind\":\"process\",\"providerGeneration\":1,\"limits\":{\"maxPageSize\":1000,\"maxResponseBytes\":8388608,\"maxWaitMilliseconds\":1000}}]}}",
            json);
    }

    [Fact]
    public void Ui_list_windows_request_and_response_match_v1_golden_json()
    {
        var request = new ObservationRequest<UiListWindowsParameters>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationMethodId.UiListWindows,
            TimeoutMilliseconds: 1000,
            new UiListWindowsParameters());
        var response = new ObservationResponse<UiWindowListResult>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationOutcome.Complete,
            new UiWindowListResult(
                DateTimeOffset.Parse("2026-08-02T01:02:03Z"),
                [
                    new ObservationUiWindow(
                        "StudioShellWindow",
                        "Asharia Studio",
                        IsVisible: true,
                        IsEnabled: true),
                ]));

        var requestJson = Encoding.UTF8.GetString(ObservationProtocolJson.WriteRequest(request));
        var responseJson = Encoding.UTF8.GetString(ObservationProtocolJson.WriteResponse(response));

        Assert.Equal(
            "{\"protocol\":{\"major\":1,\"minor\":0},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"method\":\"ui.listWindows\",\"timeoutMilliseconds\":1000,\"parameters\":{}}",
            requestJson);
        Assert.Equal(
            "{\"protocol\":{\"major\":1,\"minor\":0},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"outcome\":\"complete\",\"value\":{\"capturedAtUtc\":\"2026-08-02T01:02:03+00:00\",\"windows\":[{\"windowId\":\"StudioShellWindow\",\"name\":\"Asharia Studio\",\"isVisible\":true,\"isEnabled\":true}]}}",
            responseJson);
    }

    [Fact]
    public void Ui_read_tree_partial_response_matches_v1_golden_json()
    {
        var request = new ObservationRequest<UiReadTreeParameters>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationMethodId.UiReadTree,
            TimeoutMilliseconds: 1000,
            new UiReadTreeParameters(
                "StudioShellWindow",
                MaxDepth: 1,
                MaxNodes: 2));
        var response = new ObservationResponse<UiTreeReadResult>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationOutcome.Partial,
            new UiTreeReadResult(
                "StudioShellWindow",
                DateTimeOffset.Parse("2026-08-02T01:02:03Z"),
                IsTruncated: true,
                TruncationReason: "ui.max-nodes",
                [
                    new ObservationUiNode(
                        "StudioShellWindow",
                        ParentElementId: null,
                        Depth: 0,
                        "Asharia Studio",
                        ObservationUiRoles.Window,
                        IsVisible: true,
                        IsEnabled: true),
                    new ObservationUiNode(
                        "StudioShellStartingState",
                        "StudioShellWindow",
                        Depth: 1,
                        "Studio startup state",
                        ObservationUiRoles.Status,
                        IsVisible: true,
                        IsEnabled: true),
                ]),
            Truncation: new ObservationTruncation(
                IsTruncated: true,
                Reason: "ui.max-nodes"));

        var requestJson = Encoding.UTF8.GetString(ObservationProtocolJson.WriteRequest(request));
        var responseJson = Encoding.UTF8.GetString(ObservationProtocolJson.WriteResponse(response));

        Assert.Equal(
            "{\"protocol\":{\"major\":1,\"minor\":0},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"method\":\"ui.readTree\",\"timeoutMilliseconds\":1000,\"parameters\":{\"windowId\":\"StudioShellWindow\",\"maxDepth\":1,\"maxNodes\":2}}",
            requestJson);
        Assert.Equal(
            "{\"protocol\":{\"major\":1,\"minor\":0},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"outcome\":\"partial\",\"value\":{\"windowId\":\"StudioShellWindow\",\"capturedAtUtc\":\"2026-08-02T01:02:03+00:00\",\"isTruncated\":true,\"truncationReason\":\"ui.max-nodes\",\"nodes\":[{\"elementId\":\"StudioShellWindow\",\"depth\":0,\"name\":\"Asharia Studio\",\"role\":\"window\",\"isVisible\":true,\"isEnabled\":true},{\"elementId\":\"StudioShellStartingState\",\"parentElementId\":\"StudioShellWindow\",\"depth\":1,\"name\":\"Studio startup state\",\"role\":\"status\",\"isVisible\":true,\"isEnabled\":true}]},\"truncation\":{\"isTruncated\":true,\"reason\":\"ui.max-nodes\",\"droppedCount\":0}}",
            responseJson);
    }

    [Fact]
    public void Ui_request_identity_and_budget_bounds_fail_closed()
    {
        var invalidIdentity = new ObservationRequest<UiReadTreeParameters>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationMethodId.UiReadTree,
            TimeoutMilliseconds: 1000,
            new UiReadTreeParameters("0x1234", MaxDepth: 1, MaxNodes: 2));
        var invalidBudget = invalidIdentity with
        {
            Parameters = new UiReadTreeParameters(
                "StudioShellWindow",
                ObservationProtocolLimits.MaxUiDepth + 1,
                ObservationProtocolLimits.MaxUiNodes + 1),
        };

        Assert.Throws<ArgumentException>(() => ObservationProtocolJson.WriteRequest(invalidIdentity));
        Assert.Equal(
            "observation.request.invalid",
            ObservationProtocolJson.ReadRequest<UiReadTreeParameters>(
                SerializeUnchecked(invalidBudget),
                ObservationMethodId.UiReadTree).Failure!.Code);
    }

    [Fact]
    public void Ui_tree_requires_topological_semantics_and_matching_partial_envelope()
    {
        var invalidTree = new UiTreeReadResult(
            "StudioShellWindow",
            DateTimeOffset.Parse("2026-08-02T01:02:03Z"),
            IsTruncated: false,
            TruncationReason: null,
            [
                new ObservationUiNode(
                    "StudioShellWindow",
                    ParentElementId: null,
                    Depth: 0,
                    "Asharia Studio",
                    ObservationUiRoles.Window,
                    IsVisible: true,
                    IsEnabled: true),
                new ObservationUiNode(
                    "StudioShellStartingState",
                    "MissingParent",
                    Depth: 1,
                    "Studio startup state",
                    ObservationUiRoles.Status,
                    IsVisible: true,
                    IsEnabled: true),
            ]);
        var invalidResponse = new ObservationResponse<UiTreeReadResult>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationOutcome.Complete,
            invalidTree);
        var mismatchedPartial = invalidResponse with
        {
            Value = invalidTree with
            {
                IsTruncated = true,
                TruncationReason = "ui.max-nodes",
                Nodes = [invalidTree.Nodes[0]],
            },
        };

        Assert.Throws<ArgumentException>(() => ObservationProtocolJson.WriteResponse(invalidResponse));
        Assert.Throws<ArgumentException>(() => ObservationProtocolJson.WriteResponse(mismatchedPartial));
        Assert.Equal(
            "observation.response.invalid",
            ObservationProtocolJson.ReadResponse<UiTreeReadResult>(
                SerializeUnchecked(invalidResponse)).Failure!.Code);
    }

    [Fact]
    public void Same_major_future_minor_and_unknown_additive_fields_are_accepted()
    {
        var json = Encoding.UTF8.GetBytes(
            "{\"protocol\":{\"major\":1,\"minor\":9,\"future\":true},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"method\":\"session.describe\",\"timeoutMilliseconds\":1000,\"parameters\":{\"future\":true},\"future\":true}");

        var result = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            json,
            ObservationMethodId.SessionDescribe);

        Assert.True(result.Succeeded);
        Assert.Equal(9, result.Value!.Protocol.Minor);
    }

    [Fact]
    public void Unknown_additive_outcome_projects_to_unknown()
    {
        var json = Encoding.UTF8.GetBytes(
            "{\"protocol\":{\"major\":1,\"minor\":1},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"outcome\":\"futureOutcome\"}");

        var result = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>(json);

        Assert.True(result.Succeeded);
        Assert.Equal(ObservationOutcome.Unknown, result.Value!.Outcome);
    }

    [Fact]
    public void Typed_failure_response_roundtrips_without_losing_scope_or_correlation()
    {
        var correlationId = Guid.Parse("99999999-8888-7777-6666-555555555555");
        var scopeId = Guid.Parse("12345678-1234-1234-1234-123456789abc");
        var response = new ObservationResponse<ToolSessionDescriptor>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationOutcome.TimedOut,
            Value: null,
            new ObservationFailure(
                "observation.deadline.exceeded",
                "provider",
                "Session provider exceeded its deadline.",
                Retryable: true,
                Remediation: "Retry with a fresh session descriptor.",
                CapabilityId: "session.core",
                Scope: new ObservationScopeReference("process", scopeId, 1, 3),
                CorrelationId: correlationId,
                Attributes: [new ObservationSafeAttribute("provider", "session")]));

        var bytes = ObservationProtocolJson.WriteResponse(response);
        var result = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>(bytes);

        Assert.True(result.Succeeded);
        Assert.Equal("observation.deadline.exceeded", result.Value!.Failure!.Code);
        Assert.Equal(scopeId, result.Value.Failure.Scope!.OwnerScopeId);
        Assert.Equal(3, result.Value.Failure.Scope.ProviderGeneration);
        Assert.Equal(correlationId, result.Value.Failure.CorrelationId);
        Assert.Equal("provider", Assert.Single(result.Value.Failure.Attributes).Name);
    }

    [Fact]
    public void Incompatible_major_and_wrong_method_fail_closed_with_typed_code()
    {
        var incompatible = Request() with
        {
            Protocol = new ObservationProtocolVersion(Major: 2, Minor: 0),
        };
        var wrongMethod = Request() with
        {
            Method = ObservationMethodId.StateRead,
        };

        var incompatibleResult = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            SerializeUnchecked(incompatible),
            ObservationMethodId.SessionDescribe);
        var wrongMethodResult = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            SerializeUnchecked(wrongMethod),
            ObservationMethodId.SessionDescribe);

        Assert.Equal("observation.protocol.unsupported", incompatibleResult.Failure!.Code);
        Assert.Equal("observation.protocol.unsupported", wrongMethodResult.Failure!.Code);
    }

    [Fact]
    public void Unknown_request_method_and_invalid_local_response_outcome_fail_closed()
    {
        var unknownMethod = Request() with
        {
            Method = new ObservationMethodId("future.invoke"),
        };
        var invalidResponse = new ObservationResponse<ToolSessionDescriptor>(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationOutcome.Unknown,
            Value: null);

        var methodResult = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            SerializeUnchecked(unknownMethod),
            new ObservationMethodId("future.invoke"));

        Assert.Equal(ObservationMethodKind.Unknown, unknownMethod.Method.Kind);
        Assert.Equal("observation.protocol.unsupported", methodResult.Failure!.Code);
        Assert.Throws<ArgumentException>(() => ObservationProtocolJson.WriteResponse(invalidResponse));
    }

    [Fact]
    public void Malformed_utf8_json_and_excessive_depth_return_typed_failure()
    {
        var invalidUtf8 = new byte[] { 0x7B, 0x22, 0xC3, 0x28, 0x22, 0x7D };
        var deepParameters = string.Concat(Enumerable.Repeat("{\"x\":", 40))
            + "true"
            + string.Concat(Enumerable.Repeat("}", 40));
        var tooDeep = Encoding.UTF8.GetBytes(
            "{\"protocol\":{\"major\":1,\"minor\":0},\"requestId\":\"11111111-2222-3333-4444-555555555555\",\"studioInstanceId\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\",\"endpointGeneration\":7,\"method\":\"session.describe\",\"timeoutMilliseconds\":1000,\"parameters\":"
            + deepParameters
            + "}");

        var utf8Result = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            invalidUtf8,
            ObservationMethodId.SessionDescribe);
        var depthResult = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            tooDeep,
            ObservationMethodId.SessionDescribe);

        Assert.Equal("observation.request.malformed", utf8Result.Failure!.Code);
        Assert.Equal("observation.request.malformed", depthResult.Failure!.Code);
    }

    [Fact]
    public void Malformed_response_has_response_specific_typed_failure()
    {
        var result = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>("{"u8);

        Assert.Equal("observation.response.malformed", result.Failure!.Code);
    }

    [Fact]
    public void Request_byte_and_timeout_bounds_fail_closed()
    {
        var oversized = new byte[ObservationProtocolLimits.MaxRequestBytes + 1];
        var invalidTimeout = Request() with
        {
            TimeoutMilliseconds = ObservationProtocolLimits.MaxRequestTimeoutMilliseconds + 1,
        };

        var oversizedResult = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            oversized,
            ObservationMethodId.SessionDescribe);
        var timeoutResult = ObservationProtocolJson.ReadRequest<SessionDescribeParameters>(
            SerializeUnchecked(invalidTimeout),
            ObservationMethodId.SessionDescribe);

        Assert.Equal("observation.request.too-large", oversizedResult.Failure!.Code);
        Assert.Equal("observation.request.invalid", timeoutResult.Failure!.Code);
    }

    [Fact]
    public void Protocol_project_has_no_runtime_framework_transport_or_mutation_dependency()
    {
        var assembly = typeof(ObservationProtocolVersion).Assembly;
        Assert.Equal("Asharia.Studio.DevelopmentProtocol", assembly.GetName().Name);
        Assert.DoesNotContain(
            assembly.GetReferencedAssemblies(),
            reference => reference.Name is not null
                && (reference.Name.StartsWith("Avalonia", StringComparison.Ordinal)
                    || reference.Name.StartsWith("Asharia.Studio.Application", StringComparison.Ordinal)
                    || reference.Name.StartsWith("Asharia.Studio.EngineBridge", StringComparison.Ordinal)));

        var exportedTypes = assembly.GetExportedTypes();
        Assert.DoesNotContain(exportedTypes, type => type == typeof(IntPtr) || typeof(Delegate).IsAssignableFrom(type));
        Assert.DoesNotContain(
            new[] { "NamedPipe", "PipeStream", "LibraryImport", "DllImport", "Mcp", "Mutation", "Capture" },
            token => exportedTypes.Any(type => type.FullName?.Contains(token, StringComparison.Ordinal) == true));
    }

    private static ObservationRequest<SessionDescribeParameters> Request() =>
        new(
            ObservationProtocolVersion.Current,
            RequestId,
            InstanceId,
            EndpointGeneration: 7,
            ObservationMethodId.SessionDescribe,
            TimeoutMilliseconds: 1000,
            new SessionDescribeParameters());

    private static DevelopmentSessionManifest Manifest() =>
        new(
            SchemaVersion: 1,
            ObservationProtocolVersion.Current,
            InstanceId,
            SessionId,
            ProcessId: 4242,
            ProcessStartTimeUtc: DateTimeOffset.Parse("2026-08-01T01:02:03Z"),
            EndpointGeneration: 7,
            PipeName: "asharia_studio_aaaaaaaabbbbccccddddeeeeeeeeeeee_7",
            AttachToken: "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
            BuildIdentity: "asharia-dev-abc123",
            Configuration: "Development",
            CapabilityDigest: new string('0', 64),
            CreatedAtUtc: DateTimeOffset.Parse("2026-08-01T01:02:04Z"),
            HeartbeatUtc: DateTimeOffset.Parse("2026-08-01T01:02:04Z"));

    private static byte[] SerializeUnchecked<T>(T value) =>
        System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(
            value,
            new System.Text.Json.JsonSerializerOptions
            {
                PropertyNamingPolicy = System.Text.Json.JsonNamingPolicy.CamelCase,
                Converters =
                {
                    new TestGuidIdConverter<ObservationRequestId>(
                        static id => id.Value,
                        static id => new ObservationRequestId(id)),
                    new TestGuidIdConverter<StudioInstanceId>(
                        static id => id.Value,
                        static id => new StudioInstanceId(id)),
                    new TestGuidIdConverter<StudioSessionId>(
                        static id => id.Value,
                        static id => new StudioSessionId(id)),
                    new TestMethodIdConverter(),
                    new System.Text.Json.Serialization.JsonStringEnumConverter(
                        System.Text.Json.JsonNamingPolicy.CamelCase),
                },
            });

    private sealed class TestGuidIdConverter<T>(
        Func<T, Guid> read,
        Func<Guid, T> create) : System.Text.Json.Serialization.JsonConverter<T>
        where T : struct
    {
        public override T Read(
            ref System.Text.Json.Utf8JsonReader reader,
            Type typeToConvert,
            System.Text.Json.JsonSerializerOptions options) =>
            create(reader.GetGuid());

        public override void Write(
            System.Text.Json.Utf8JsonWriter writer,
            T value,
            System.Text.Json.JsonSerializerOptions options) =>
            writer.WriteStringValue(read(value));
    }

    private sealed class TestMethodIdConverter
        : System.Text.Json.Serialization.JsonConverter<ObservationMethodId>
    {
        public override ObservationMethodId Read(
            ref System.Text.Json.Utf8JsonReader reader,
            Type typeToConvert,
            System.Text.Json.JsonSerializerOptions options) =>
            new(reader.GetString()!);

        public override void Write(
            System.Text.Json.Utf8JsonWriter writer,
            ObservationMethodId value,
            System.Text.Json.JsonSerializerOptions options) =>
            writer.WriteStringValue(value.Value);
    }
}
