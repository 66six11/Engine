using System;
using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Asharia.Studio.DevelopmentProtocol;

public static class ObservationProtocolJson
{
    private static readonly JsonSerializerOptions Options = CreateOptions();

    public static byte[] WriteRequest<TParameters>(
        ObservationRequest<TParameters> request)
        where TParameters : class
    {
        ArgumentNullException.ThrowIfNull(request);
        var failure = ValidateRequest(request, request.Method);
        if (failure is not null)
        {
            throw new ArgumentException(failure.Message, nameof(request));
        }

        return SerializeBounded(request, ObservationProtocolLimits.MaxRequestBytes, "request");
    }

    public static byte[] WriteResponse<TValue>(
        ObservationResponse<TValue> response)
        where TValue : class
    {
        ArgumentNullException.ThrowIfNull(response);
        if (response.Failure is { Attributes.IsDefault: true } responseFailure)
        {
            response = response with
            {
                Failure = responseFailure with { Attributes = [] },
            };
        }

        var failure = ValidateResponse(response, allowUnknownOutcome: false);
        if (failure is not null)
        {
            throw new ArgumentException(failure.Message, nameof(response));
        }

        return SerializeBounded(response, ObservationProtocolLimits.MaxResponseBytes, "response");
    }

    public static byte[] WriteHandshakeRequest(ObservationHandshakeRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        var failure = ValidateHandshakeRequest(request);
        if (failure is not null)
        {
            throw new ArgumentException(failure.Message, nameof(request));
        }

        return SerializeBounded(
            request,
            ObservationProtocolLimits.MaxRequestBytes,
            "handshake request");
    }

    public static ObservationProtocolReadResult<ObservationHandshakeRequest> ReadHandshakeRequest(
        ReadOnlySpan<byte> utf8Json)
    {
        if (utf8Json.Length > ObservationProtocolLimits.MaxRequestBytes)
        {
            return ObservationProtocolReadResult<ObservationHandshakeRequest>.Rejected(
                Failure(
                    "observation.request.too-large",
                    "protocol",
                    $"Handshake size {utf8Json.Length} exceeds the {ObservationProtocolLimits.MaxRequestBytes}-byte limit."));
        }

        try
        {
            var request = JsonSerializer.Deserialize<ObservationHandshakeRequest>(utf8Json, Options);
            if (request is null)
            {
                return ObservationProtocolReadResult<ObservationHandshakeRequest>.Rejected(
                    Malformed("Handshake JSON produced no typed envelope."));
            }

            var failure = ValidateHandshakeRequest(request);
            return failure is null
                ? ObservationProtocolReadResult<ObservationHandshakeRequest>.Success(request)
                : ObservationProtocolReadResult<ObservationHandshakeRequest>.Rejected(failure);
        }
        catch (JsonException error)
        {
            return ObservationProtocolReadResult<ObservationHandshakeRequest>.Rejected(
                Malformed($"Handshake JSON is not a valid v1 envelope: {error.Message}"));
        }
        catch (NotSupportedException error)
        {
            return ObservationProtocolReadResult<ObservationHandshakeRequest>.Rejected(
                Malformed($"Handshake JSON uses an unsupported type: {error.Message}"));
        }
    }

    public static byte[] WriteSessionManifest(DevelopmentSessionManifest manifest)
    {
        ArgumentNullException.ThrowIfNull(manifest);
        var failure = ValidateSessionManifest(manifest);
        if (failure is not null)
        {
            throw new ArgumentException(failure.Message, nameof(manifest));
        }

        return SerializeBounded(
            manifest,
            ObservationProtocolLimits.MaxSessionManifestBytes,
            "session manifest");
    }

    public static ObservationProtocolReadResult<DevelopmentSessionManifest> ReadSessionManifest(
        ReadOnlySpan<byte> utf8Json)
    {
        if (utf8Json.Length > ObservationProtocolLimits.MaxSessionManifestBytes)
        {
            return ObservationProtocolReadResult<DevelopmentSessionManifest>.Rejected(
                Failure(
                    "observation.manifest.too-large",
                    "protocol",
                    $"Session manifest exceeds the {ObservationProtocolLimits.MaxSessionManifestBytes}-byte limit."));
        }

        try
        {
            var manifest = JsonSerializer.Deserialize<DevelopmentSessionManifest>(utf8Json, Options);
            if (manifest is null)
            {
                return ObservationProtocolReadResult<DevelopmentSessionManifest>.Rejected(
                    Failure(
                        "observation.manifest.malformed",
                        "protocol",
                        "Session manifest JSON produced no typed value."));
            }

            var failure = ValidateSessionManifest(manifest);
            return failure is null
                ? ObservationProtocolReadResult<DevelopmentSessionManifest>.Success(manifest)
                : ObservationProtocolReadResult<DevelopmentSessionManifest>.Rejected(failure);
        }
        catch (JsonException error)
        {
            return ObservationProtocolReadResult<DevelopmentSessionManifest>.Rejected(
                Failure(
                    "observation.manifest.malformed",
                    "protocol",
                    $"Session manifest JSON is invalid: {error.Message}"));
        }
        catch (NotSupportedException error)
        {
            return ObservationProtocolReadResult<DevelopmentSessionManifest>.Rejected(
                Failure(
                    "observation.manifest.malformed",
                    "protocol",
                    $"Session manifest JSON uses an unsupported type: {error.Message}"));
        }
    }

    public static ObservationProtocolReadResult<ObservationRequest<TParameters>> ReadRequest<TParameters>(
        ReadOnlySpan<byte> utf8Json,
        ObservationMethodId expectedMethod)
        where TParameters : class
    {
        if (utf8Json.Length > ObservationProtocolLimits.MaxRequestBytes)
        {
            return ObservationProtocolReadResult<ObservationRequest<TParameters>>.Rejected(
                Failure(
                    "observation.request.too-large",
                    "protocol",
                    $"Request size {utf8Json.Length} exceeds the {ObservationProtocolLimits.MaxRequestBytes}-byte limit."));
        }

        try
        {
            var request = JsonSerializer.Deserialize<ObservationRequest<TParameters>>(utf8Json, Options);
            if (request is null)
            {
                return ObservationProtocolReadResult<ObservationRequest<TParameters>>.Rejected(
                    Malformed("Request JSON produced no typed envelope."));
            }

            var failure = ValidateRequest(request, expectedMethod);
            return failure is null
                ? ObservationProtocolReadResult<ObservationRequest<TParameters>>.Success(request)
                : ObservationProtocolReadResult<ObservationRequest<TParameters>>.Rejected(failure);
        }
        catch (JsonException error)
        {
            return ObservationProtocolReadResult<ObservationRequest<TParameters>>.Rejected(
                Malformed($"Request JSON is not a valid v1 envelope: {error.Message}"));
        }
        catch (NotSupportedException error)
        {
            return ObservationProtocolReadResult<ObservationRequest<TParameters>>.Rejected(
                Malformed($"Request JSON uses an unsupported type: {error.Message}"));
        }
    }

    public static ObservationProtocolReadResult<ObservationResponse<TValue>> ReadResponse<TValue>(
        ReadOnlySpan<byte> utf8Json)
        where TValue : class
    {
        if (utf8Json.Length > ObservationProtocolLimits.MaxResponseBytes)
        {
            return ObservationProtocolReadResult<ObservationResponse<TValue>>.Rejected(
                Failure(
                    "observation.response.truncated",
                    "protocol",
                    $"Response size {utf8Json.Length} exceeds the {ObservationProtocolLimits.MaxResponseBytes}-byte limit."));
        }

        try
        {
            var response = JsonSerializer.Deserialize<ObservationResponse<TValue>>(utf8Json, Options);
            if (response is null)
            {
                return ObservationProtocolReadResult<ObservationResponse<TValue>>.Rejected(
                    ResponseMalformed("Response JSON produced no typed envelope."));
            }

            var failure = ValidateResponse(response, allowUnknownOutcome: true);
            return failure is null
                ? ObservationProtocolReadResult<ObservationResponse<TValue>>.Success(response)
                : ObservationProtocolReadResult<ObservationResponse<TValue>>.Rejected(failure);
        }
        catch (JsonException error)
        {
            return ObservationProtocolReadResult<ObservationResponse<TValue>>.Rejected(
                ResponseMalformed($"Response JSON is not a valid v1 envelope: {error.Message}"));
        }
        catch (NotSupportedException error)
        {
            return ObservationProtocolReadResult<ObservationResponse<TValue>>.Rejected(
                ResponseMalformed($"Response JSON uses an unsupported type: {error.Message}"));
        }
    }

    private static ObservationFailure? ValidateRequest<TParameters>(
        ObservationRequest<TParameters> request,
        ObservationMethodId expectedMethod)
        where TParameters : class
    {
        if (request.Protocol.Major != ObservationProtocolVersion.Current.Major
            || request.Protocol.Minor < 0)
        {
            return Failure(
                "observation.protocol.unsupported",
                "protocol",
                $"Protocol {request.Protocol.Major}.{request.Protocol.Minor} is incompatible with v{ObservationProtocolVersion.Current.Major}.x.");
        }

        if (request.RequestId.Value == Guid.Empty
            || request.StudioInstanceId.Value == Guid.Empty
            || request.EndpointGeneration <= 0
            || request.TimeoutMilliseconds <= 0
            || request.TimeoutMilliseconds > ObservationProtocolLimits.MaxRequestTimeoutMilliseconds
            || request.Parameters is null)
        {
            return Failure(
                "observation.request.invalid",
                "protocol",
                "Request identity, generation, timeout, and typed parameters must be valid and non-empty.");
        }

        if (request.Method.Kind == ObservationMethodKind.Unknown)
        {
            return Failure(
                "observation.protocol.unsupported",
                "protocol",
                $"Method '{request.Method.Value}' is not part of the v1 Observe allowlist.");
        }

        if (!string.Equals(
                request.Method.Value,
                expectedMethod.Value,
                StringComparison.Ordinal))
        {
            return Failure(
                "observation.protocol.unsupported",
                "protocol",
                $"Method '{request.Method.Value}' is not the expected '{expectedMethod.Value}' contract.");
        }

        var parameterError = request.Parameters switch
        {
            UiReadTreeParameters parameters => ObservationUiContractValidation.Validate(parameters),
            _ => null,
        };
        if (parameterError is not null)
        {
            return Failure(
                "observation.request.invalid",
                "protocol",
                parameterError);
        }

        return null;
    }

    private static ObservationFailure? ValidateHandshakeRequest(
        ObservationHandshakeRequest request)
    {
        if (request.Protocol.Major != ObservationProtocolVersion.Current.Major
            || request.Protocol.Minor < 0)
        {
            return Failure(
                "observation.protocol.unsupported",
                "protocol",
                $"Protocol {request.Protocol.Major}.{request.Protocol.Minor} is incompatible with v{ObservationProtocolVersion.Current.Major}.x.");
        }

        if (request.RequestId.Value == Guid.Empty
            || request.StudioInstanceId.Value == Guid.Empty
            || request.EndpointGeneration <= 0
            || string.IsNullOrWhiteSpace(request.AttachToken)
            || request.AttachToken.Length > ObservationProtocolLimits.MaxAttachTokenCharacters)
        {
            return Failure(
                "observation.handshake.invalid",
                "protocol",
                "Handshake identity, generation, and bounded attach token must be valid and non-empty.");
        }

        return null;
    }

    private static ObservationFailure? ValidateSessionManifest(
        DevelopmentSessionManifest manifest)
    {
        if (manifest.SchemaVersion != 1
            || manifest.Protocol.Major != ObservationProtocolVersion.Current.Major
            || manifest.Protocol.Minor < 0)
        {
            return Failure(
                "observation.manifest.unsupported",
                "protocol",
                "Session manifest schema or protocol version is unsupported.");
        }

        if (manifest.StudioInstanceId.Value == Guid.Empty
            || manifest.StudioSessionId.Value == Guid.Empty
            || manifest.ProcessId <= 0
            || manifest.ProcessStartTimeUtc == default
            || manifest.EndpointGeneration <= 0
            || string.IsNullOrWhiteSpace(manifest.PipeName)
            || manifest.PipeName.Length > 128
            || string.IsNullOrWhiteSpace(manifest.AttachToken)
            || manifest.AttachToken.Length > ObservationProtocolLimits.MaxAttachTokenCharacters
            || string.IsNullOrWhiteSpace(manifest.BuildIdentity)
            || manifest.BuildIdentity.Length > 256
            || string.IsNullOrWhiteSpace(manifest.Configuration)
            || manifest.Configuration.Length > 64
            || string.IsNullOrWhiteSpace(manifest.CapabilityDigest)
            || manifest.CapabilityDigest.Length != 64
            || manifest.CreatedAtUtc == default
            || manifest.HeartbeatUtc < manifest.CreatedAtUtc)
        {
            return Failure(
                "observation.manifest.invalid",
                "protocol",
                "Session manifest identity, endpoint, build, capability, and time fields must be bounded and valid.");
        }

        byte[] digestBytes;
        byte[] tokenBytes;
        try
        {
            digestBytes = Convert.FromHexString(manifest.CapabilityDigest);
            tokenBytes = Convert.FromBase64String(manifest.AttachToken);
        }
        catch (FormatException)
        {
            return Failure(
                "observation.manifest.invalid",
                "protocol",
                "Session manifest digest and attach token must use canonical encodings.");
        }

        try
        {
            if (digestBytes.Length != 32
                || tokenBytes.Length != 32
                || !string.Equals(
                    Convert.ToHexString(digestBytes),
                    manifest.CapabilityDigest,
                    StringComparison.Ordinal)
                || !string.Equals(
                    Convert.ToBase64String(tokenBytes),
                    manifest.AttachToken,
                    StringComparison.Ordinal))
            {
                return Failure(
                    "observation.manifest.invalid",
                    "protocol",
                    "Session manifest digest and attach token must use canonical 256-bit encodings.");
            }
        }
        finally
        {
            CryptographicOperations.ZeroMemory(tokenBytes);
        }

        return null;
    }

    private static ObservationFailure? ValidateResponse<TValue>(
        ObservationResponse<TValue> response,
        bool allowUnknownOutcome)
        where TValue : class
    {
        if (response.Protocol.Major != ObservationProtocolVersion.Current.Major
            || response.Protocol.Minor < 0)
        {
            return Failure(
                "observation.protocol.unsupported",
                "protocol",
                $"Protocol {response.Protocol.Major}.{response.Protocol.Minor} is incompatible with v{ObservationProtocolVersion.Current.Major}.x.");
        }

        if (response.RequestId.Value == Guid.Empty
            || response.StudioInstanceId.Value == Guid.Empty
            || response.EndpointGeneration <= 0)
        {
            return Failure(
                "observation.response.invalid",
                "protocol",
                "Response identity and endpoint generation must be valid and non-empty.");
        }

        if (response.Outcome == ObservationOutcome.Unknown)
        {
            return allowUnknownOutcome
                ? null
                : Failure(
                    "observation.response.invalid",
                    "protocol",
                    "A local v1 producer cannot emit an unknown outcome.");
        }

        var expectsValue = response.Outcome is ObservationOutcome.Complete or ObservationOutcome.Partial;
        var expectsFailure = response.Outcome is ObservationOutcome.Failed
            or ObservationOutcome.Cancelled
            or ObservationOutcome.TimedOut;
        if ((expectsValue && response.Value is null)
            || (expectsFailure && response.Failure is null)
            || (expectsFailure && response.Value is not null)
            || (response.Outcome == ObservationOutcome.Complete && response.Failure is not null)
            || (response.Outcome == ObservationOutcome.Complete
                && response.Truncation?.IsTruncated == true))
        {
            return Failure(
                "observation.response.invalid",
                "protocol",
                "Response value/failure fields do not match the declared outcome.");
        }

        var valueError = response.Value switch
        {
            UiWindowListResult value => ObservationUiContractValidation.Validate(value),
            UiTreeReadResult value => ObservationUiContractValidation.Validate(value),
            _ => null,
        };
        if (valueError is not null)
        {
            return Failure(
                "observation.response.invalid",
                "protocol",
                valueError);
        }

        if (response.Value is UiTreeReadResult tree
            && (tree.IsTruncated != (response.Outcome == ObservationOutcome.Partial)
                || tree.IsTruncated != (response.Truncation?.IsTruncated == true)))
        {
            return Failure(
                "observation.response.invalid",
                "protocol",
                "UI tree value and response partial/truncation semantics must agree.");
        }

        return null;
    }

    private static byte[] SerializeBounded<T>(T value, int maxBytes, string kind)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(value, Options);
        if (bytes.Length > maxBytes)
        {
            throw new ArgumentOutOfRangeException(
                nameof(value),
                $"Serialized {kind} size {bytes.Length} exceeds the {maxBytes}-byte limit.");
        }

        return bytes;
    }

    private static JsonSerializerOptions CreateOptions()
    {
        var options = new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            MaxDepth = ObservationProtocolLimits.MaxJsonDepth,
            NumberHandling = JsonNumberHandling.Strict,
            UnmappedMemberHandling = JsonUnmappedMemberHandling.Skip,
            WriteIndented = false,
        };
        options.Converters.Add(new ObservationGuidIdConverter<ObservationRequestId>(
            static value => value.Value,
            static value => new ObservationRequestId(value)));
        options.Converters.Add(new ObservationGuidIdConverter<StudioInstanceId>(
            static value => value.Value,
            static value => new StudioInstanceId(value)));
        options.Converters.Add(new ObservationGuidIdConverter<StudioSessionId>(
            static value => value.Value,
            static value => new StudioSessionId(value)));
        options.Converters.Add(new ObservationMethodIdConverter());
        options.Converters.Add(new ObservationOutcomeConverter());
        return options;
    }

    private static ObservationFailure Malformed(string message) =>
        Failure("observation.request.malformed", "protocol", message);

    private static ObservationFailure ResponseMalformed(string message) =>
        Failure("observation.response.malformed", "protocol", message);

    private static ObservationFailure Failure(string code, string category, string message) =>
        new(code, category, message, Retryable: false);

    private sealed class ObservationGuidIdConverter<T>(
        Func<T, Guid> readValue,
        Func<Guid, T> create) : JsonConverter<T>
        where T : struct
    {
        public override T Read(
            ref Utf8JsonReader reader,
            Type typeToConvert,
            JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.String
                || !Guid.TryParseExact(reader.GetString(), "D", out var value))
            {
                throw new JsonException("Expected a canonical D-format GUID string.");
            }

            return create(value);
        }

        public override void Write(
            Utf8JsonWriter writer,
            T value,
            JsonSerializerOptions options) =>
            writer.WriteStringValue(readValue(value).ToString("D", CultureInfo.InvariantCulture));
    }

    private sealed class ObservationMethodIdConverter : JsonConverter<ObservationMethodId>
    {
        public override ObservationMethodId Read(
            ref Utf8JsonReader reader,
            Type typeToConvert,
            JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.String)
            {
                throw new JsonException("Expected a stable observation method string ID.");
            }

            try
            {
                return new ObservationMethodId(reader.GetString()!);
            }
            catch (ArgumentException error)
            {
                throw new JsonException("Observation method ID is invalid.", error);
            }
        }

        public override void Write(
            Utf8JsonWriter writer,
            ObservationMethodId value,
            JsonSerializerOptions options) =>
            writer.WriteStringValue(value.Value);
    }

    private sealed class ObservationOutcomeConverter : JsonConverter<ObservationOutcome>
    {
        public override ObservationOutcome Read(
            ref Utf8JsonReader reader,
            Type typeToConvert,
            JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.String)
            {
                throw new JsonException("Expected a stable observation outcome string ID.");
            }

            return reader.GetString() switch
            {
                "complete" => ObservationOutcome.Complete,
                "partial" => ObservationOutcome.Partial,
                "failed" => ObservationOutcome.Failed,
                "cancelled" => ObservationOutcome.Cancelled,
                "timedOut" => ObservationOutcome.TimedOut,
                _ => ObservationOutcome.Unknown,
            };
        }

        public override void Write(
            Utf8JsonWriter writer,
            ObservationOutcome value,
            JsonSerializerOptions options) =>
            writer.WriteStringValue(value switch
            {
                ObservationOutcome.Complete => "complete",
                ObservationOutcome.Partial => "partial",
                ObservationOutcome.Failed => "failed",
                ObservationOutcome.Cancelled => "cancelled",
                ObservationOutcome.TimedOut => "timedOut",
                _ => "unknown",
            });
    }
}
