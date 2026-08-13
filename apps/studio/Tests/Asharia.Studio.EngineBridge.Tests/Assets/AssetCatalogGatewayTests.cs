using System;
using System.Buffers;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.EngineBridge.Assets;
using Asharia.Studio.EngineBridge.Assets.Abi;
using Xunit;

namespace Asharia.Studio.EngineBridge.Tests.Assets;

public sealed class AssetCatalogGatewayTests
{
    private const int MaximumResponseCapacity = 16 * 1024 * 1024 + 64 * 1024;
    private static readonly Guid TestProjectId =
        Guid.Parse("c66c5ec7-5c39-4613-84ed-77d186defd65");

    [Fact]
    public async Task Query_forwards_bounded_v1_request_and_parses_complete_snapshot()
    {
        var guid = Guid.NewGuid();
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(guid),
        };
        var time = new DateTimeOffset(2026, 8, 13, 10, 30, 0, TimeSpan.Zero);
        var gateway = new AssetCatalogGateway(api, new FixedTimeProvider(time));
        var scope = Scope();

        var result = await gateway.QueryAsync(scope);

        Assert.True(result.Succeeded);
        Assert.Equal(scope.ProjectFilePath, api.ProjectPath);
        Assert.Equal("editor-preview", api.TargetProfile);
        Assert.Equal(AssetCatalogNativeAbi.Version, api.Request.Header.AbiVersion);
        Assert.Equal(10_000UL, api.Request.Limits.MaxSourceFiles);
        Assert.Equal(16UL * 1024 * 1024, api.Request.Limits.MaxResponseBytes);
        var snapshot = result.Snapshot!;
        Assert.Equal(TestProjectId, snapshot.ProjectId);
        Assert.Equal(AssetCatalogSnapshotState.Ready, snapshot.State);
        Assert.Equal(time, snapshot.CapturedAtUtc);
        Assert.Single(snapshot.SourceRoots);
        Assert.Equal(3, snapshot.Navigation.Length);
        var row = Assert.Single(snapshot.Entries);
        Assert.Equal(guid, row.AssetGuid);
        Assert.Equal(AssetCatalogProductState.Current, row.ProductState);
        Assert.Equal("hero-idle", Assert.Single(row.SubAssets).StableId);
        Assert.Equal(1, api.CallCount);
    }

    [Fact]
    public async Task Query_uses_one_fixed_bounded_buffer_and_returns_it_to_the_pool()
    {
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(Guid.NewGuid(), paddingLength: 64_000),
        };
        var pool = new TrackingArrayPool();
        var gateway = new AssetCatalogGateway(api, TimeProvider.System, pool);

        var result = await gateway.QueryAsync(Scope());

        Assert.True(result.Succeeded);
        Assert.Equal(1, api.CallCount);
        Assert.Equal((ulong)MaximumResponseCapacity, api.LastResponseCapacity);
        Assert.Equal(MaximumResponseCapacity, pool.LastMinimumLength);
        Assert.Equal(1, pool.RentCount);
        Assert.Equal(1, pool.ReturnCount);
        Assert.True(pool.LastClearArray);
    }

    [Fact]
    public async Task Buffer_too_small_is_a_single_call_protocol_failure()
    {
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(Guid.NewGuid()),
            CallStatusOverride = AssetCatalogNativeStatus.BufferTooSmall,
        };
        var pool = new TrackingArrayPool();
        var gateway = new AssetCatalogGateway(api, TimeProvider.System, pool);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidResponse, result.Failure!.Kind);
        Assert.Equal(1, api.CallCount);
        Assert.Equal(1, pool.ReturnCount);
        Assert.True(pool.LastClearArray);
    }

    [Fact]
    public async Task Native_failure_maps_without_parsing_payload()
    {
        var api = new StubAssetCatalogNativeApi
        {
            OperationStatus = AssetCatalogNativeStatus.LimitExceeded,
            Message = "The source-file limit was exceeded.",
        };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.LimitExceeded, result.Failure!.Kind);
        Assert.Equal(api.Message, result.Failure.Message);
        Assert.Equal(1, api.CallCount);
    }

    [Fact]
    public async Task Concurrent_queries_are_serialized_and_cancelled_waiters_never_enter_native()
    {
        using var firstEntered = new ManualResetEventSlim();
        using var releaseFirst = new ManualResetEventSlim();
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(Guid.NewGuid()),
            BeforeQuery = call =>
            {
                if (call == 1)
                {
                    firstEntered.Set();
                    releaseFirst.Wait(TimeSpan.FromSeconds(2));
                }
            },
        };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);
        var first = gateway.QueryAsync(Scope()).AsTask();
        Assert.True(firstEntered.Wait(TimeSpan.FromSeconds(2)));
        using var cancellation = new CancellationTokenSource();
        var cancelled = gateway.QueryAsync(Scope(), cancellation.Token).AsTask();
        cancellation.Cancel();
        releaseFirst.Set();

        Assert.True((await first).Succeeded);
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => cancelled);
        Assert.Equal(1, api.CallCount);
        Assert.Equal(1, api.MaximumConcurrentCalls);
    }

    [Theory]
    [InlineData("wrong-schema", 1, "ready")]
    [InlineData("com.asharia.editor.assetCatalogSnapshot", 2, "ready")]
    [InlineData("com.asharia.editor.assetCatalogSnapshot", 1, "unknown")]
    public async Task Invalid_schema_or_enum_fails_closed(
        string schema,
        int version,
        string state)
    {
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(
                Guid.NewGuid(),
                schema: schema,
                schemaVersion: version,
                state: state),
        };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidResponse, result.Failure!.Kind);
        Assert.Equal(1, api.CallCount);
    }

    [Fact]
    public async Task Missing_project_id_fails_closed()
    {
        var json = SnapshotJson(Guid.NewGuid()).Replace(
            $"\"projectId\":\"{TestProjectId:D}\",",
            string.Empty,
            StringComparison.Ordinal);
        var api = new StubAssetCatalogNativeApi { Payload = json };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidResponse, result.Failure!.Kind);
        Assert.Equal(1, api.CallCount);
    }

    [Fact]
    public async Task Project_id_from_another_scope_fails_closed()
    {
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(Guid.NewGuid(), projectId: Guid.NewGuid()),
        };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidResponse, result.Failure!.Kind);
        Assert.Equal(1, api.CallCount);
    }

    [Fact]
    public async Task Project_file_outside_project_root_is_rejected_before_native()
    {
        var valid = Scope();
        var outsideRoot = OperatingSystem.IsWindows()
            ? "C:\\Projects\\Other"
            : "/projects/other";
        var scope = new AssetCatalogQueryScope(
            valid.SessionId,
            valid.ProjectId,
            valid.ProjectRootPath,
            System.IO.Path.Combine(outsideRoot, "asharia.project.json"),
            valid.TargetProfile);
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(Guid.NewGuid()),
        };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(scope);

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidInput, result.Failure!.Kind);
        Assert.Equal(0, api.CallCount);
    }

    [Fact]
    public async Task Unknown_property_and_context_mismatch_fail_closed()
    {
        var json = SnapshotJson(Guid.NewGuid())
            .Replace("\"schemaVersion\":1", "\"schemaVersion\":1,\"extra\":true", StringComparison.Ordinal);
        var api = new StubAssetCatalogNativeApi { Payload = json };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidResponse, result.Failure!.Kind);
        Assert.Equal(1, api.CallCount);
    }

    [Fact]
    public async Task Malformed_transport_span_fails_closed()
    {
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(Guid.NewGuid()),
            ResultMutation = result => result with
            {
                PayloadJsonUtf8 = new AssetCatalogNativeTextSpan(
                    result.RequiredByteLength + 1,
                    result.PayloadJsonUtf8.ByteLength),
            },
        };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidResponse, result.Failure!.Kind);
        Assert.Equal(1, api.CallCount);
    }

    [Fact]
    public async Task Malformed_single_call_result_fails_closed()
    {
        var api = new StubAssetCatalogNativeApi
        {
            Payload = SnapshotJson(Guid.NewGuid(), paddingLength: 64_000),
            ResultMutation = result => result with
            {
                PayloadJsonUtf8 = new AssetCatalogNativeTextSpan(
                    result.RequiredByteLength + 1,
                    result.PayloadJsonUtf8.ByteLength),
            },
        };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidResponse, result.Failure!.Kind);
        Assert.Equal(1, api.CallCount);
    }

    [Theory]
    [InlineData("\"subAssetCount\":1", "\"subAssetCount\":0")]
    [InlineData(
        "\"assetRole\":\"texture\",\"subAssetCount\":1",
        "\"assetRole\":\"wrong\",\"subAssetCount\":1")]
    public async Task Navigation_and_rows_must_describe_the_same_asset_facts(
        string before,
        string after)
    {
        var json = SnapshotJson(Guid.NewGuid()).Replace(
            before,
            after,
            StringComparison.Ordinal);
        var api = new StubAssetCatalogNativeApi { Payload = json };
        var gateway = new AssetCatalogGateway(api, TimeProvider.System);

        var result = await gateway.QueryAsync(Scope());

        Assert.False(result.Succeeded);
        Assert.Equal(AssetCatalogQueryFailureKind.InvalidResponse, result.Failure!.Kind);
        Assert.Equal(1, api.CallCount);
    }

    [Fact]
    public void Managed_layout_matches_native_editor_content_v1()
    {
        Assert.Equal(8, Marshal.SizeOf<AssetCatalogNativeAbiHeader>());
        Assert.Equal(16, Marshal.SizeOf<AssetCatalogNativeStringView>());
        Assert.Equal(32, Marshal.SizeOf<AssetCatalogNativeLimits>());
        Assert.Equal(88, Marshal.SizeOf<AssetCatalogNativeQueryRequest>());
        Assert.Equal(16, Marshal.SizeOf<AssetCatalogNativeTextSpan>());
        Assert.Equal(56, Marshal.SizeOf<AssetCatalogNativeResult>());
        Assert.Equal(56, OffsetOf<AssetCatalogNativeQueryRequest>(
            nameof(AssetCatalogNativeQueryRequest.Limits)));
        Assert.Equal(16, OffsetOf<AssetCatalogNativeResult>(
            nameof(AssetCatalogNativeResult.RequiredByteLength)));
        Assert.Equal(24, OffsetOf<AssetCatalogNativeResult>(
            nameof(AssetCatalogNativeResult.PayloadJsonUtf8)));
        Assert.Equal(40, OffsetOf<AssetCatalogNativeResult>(
            nameof(AssetCatalogNativeResult.MessageUtf8)));
    }

    private static AssetCatalogQueryScope Scope()
    {
        var root = OperatingSystem.IsWindows()
            ? "C:\\Projects\\Sample"
            : "/projects/sample";
        return new AssetCatalogQueryScope(
            ProjectSessionId.CreateNew(),
            TestProjectId,
            root,
            System.IO.Path.Combine(root, "asharia.project.json"),
            "editor-preview");
    }

    private static string SnapshotJson(
        Guid guid,
        int paddingLength = 0,
        string schema = "com.asharia.editor.assetCatalogSnapshot",
        int schemaVersion = 1,
        string state = "ready",
        Guid? projectId = null)
    {
        var root = Scope().ProjectFilePath;
        var padding = new string('x', paddingLength);
        return $$"""
        {
          "schema":"{{schema}}",
          "schemaVersion":{{schemaVersion}},
          "state":"{{state}}",
          "projectId":"{{projectId ?? TestProjectId:D}}",
          "projectFile":"{{Json(root)}}",
          "productManifestFile":"",
          "targetProfile":"editor-preview",
          "sourceRoots":[{
            "name":"Assets","sourcePathPrefix":"Assets","directory":"Assets","resolvedDirectory":"{{Json(System.IO.Path.GetDirectoryName(root)!)}}"
          }],
          "navigationNodes":[
            {"kind":"source-root","key":"source-root:0","parentKey":"","displayName":"Assets","scopePath":"Assets","sourcePath":"","sourceRootName":"Assets","sourceRootPrefix":"Assets","sourceRootDirectory":"Assets","guid":"","stableId":"","assetType":"","importer":"","extension":"","importProfile":"","assetRole":"","subAssetCount":0,"productState":"not-tracked"},
            {"kind":"asset","key":"asset:Assets/hero.png","parentKey":"source-root:0","displayName":"hero.png","scopePath":"Assets","sourcePath":"Assets/hero.png","sourceRootName":"Assets","sourceRootPrefix":"Assets","sourceRootDirectory":"Assets","guid":"{{guid:D}}","stableId":"","assetType":"com.asharia.asset.Texture2D","importer":"asharia.texture","extension":".png","importProfile":"texture2d","assetRole":"texture","subAssetCount":1,"productState":"ready"},
            {"kind":"sub-asset","key":"sub:hero-idle","parentKey":"asset:Assets/hero.png","displayName":"Hero Idle","scopePath":"Assets","sourcePath":"Assets/hero.png","sourceRootName":"Assets","sourceRootPrefix":"Assets","sourceRootDirectory":"Assets","guid":"{{guid:D}}","stableId":"hero-idle","assetType":"com.asharia.asset.Texture2D","importer":"asharia.texture","extension":".png","importProfile":"texture2d","assetRole":"sprite","subAssetCount":0,"productState":"ready"}
          ],
          "rows":[{
            "guid":"{{guid:D}}","sourcePath":"Assets/hero.png","sourceRootName":"Assets","sourceRootPrefix":"Assets","sourceRootDirectory":"Assets","sourceFilePath":"Assets/hero.png{{padding}}","metadataFilePath":"Assets/hero.png.ameta","displayName":"hero.png","extension":".png","assetType":"com.asharia.asset.Texture2D","importer":"asharia.texture","importerVersion":1,"importProfile":"texture2d","assetRole":"texture","productState":"ready","currentProductCount":1,"staleProductCount":0,
            "subAssets":[{"stableId":"hero-idle","displayName":"Hero Idle","assetRole":"sprite"}],
            "diagnostics":[]
          }],
          "diagnostics":[]
        }
        """;
    }

    private static string Json(string value) =>
        value.Replace("\\", "\\\\", StringComparison.Ordinal);

    private static int OffsetOf<T>(string propertyName)
    {
        var field = typeof(T).GetField(
            $"<{propertyName}>k__BackingField",
            BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException(
                $"ABI backing field for '{propertyName}' is missing.");
        return Marshal.OffsetOf<T>(field.Name).ToInt32();
    }

    private sealed class FixedTimeProvider(DateTimeOffset value) : TimeProvider
    {
        public override DateTimeOffset GetUtcNow() => value;
    }

    private sealed class StubAssetCatalogNativeApi : IAssetCatalogNativeApi
    {
        public string Payload { get; set; } = string.Empty;
        public string Message { get; set; } = string.Empty;
        public AssetCatalogNativeStatus OperationStatus { get; set; } =
            AssetCatalogNativeStatus.Success;
        public AssetCatalogNativeStatus? CallStatusOverride { get; set; }
        public Func<AssetCatalogNativeResult, AssetCatalogNativeResult>? ResultMutation
        { get; set; }
        public Action<int>? BeforeQuery { get; set; }
        private int callCount_;
        private int activeCalls_;
        private int maximumConcurrentCalls_;
        public int CallCount => Volatile.Read(ref callCount_);
        public int MaximumConcurrentCalls => Volatile.Read(ref maximumConcurrentCalls_);
        public ulong LastResponseCapacity { get; private set; }
        public string ProjectPath { get; private set; } = string.Empty;
        public string TargetProfile { get; private set; } = string.Empty;
        public AssetCatalogNativeQueryRequest Request { get; private set; }

        public AssetCatalogNativeStatus Query(
            in AssetCatalogNativeQueryRequest request,
            nint responseUtf8,
            ulong responseCapacity,
            out AssetCatalogNativeResult result,
            ulong resultCapacity)
        {
            var call = Interlocked.Increment(ref callCount_);
            var active = Interlocked.Increment(ref activeCalls_);
            UpdateMaximum(active);
            BeforeQuery?.Invoke(call);
            try
            {
                LastResponseCapacity = responseCapacity;
                Request = request;
                ProjectPath = Read(request.ProjectPathUtf8);
                TargetProfile = Read(request.TargetProfileUtf8);
                var payload = OperationStatus == AssetCatalogNativeStatus.Success
                    ? Encoding.UTF8.GetBytes(Payload)
                    : [];
                var message = OperationStatus == AssetCatalogNativeStatus.Success
                    ? []
                    : Encoding.UTF8.GetBytes(Message);
                var required = checked((ulong)(payload.Length + message.Length));
                result = new AssetCatalogNativeResult(
                    new AssetCatalogNativeAbiHeader(
                        AssetCatalogNativeAbi.Version,
                        AssetCatalogNativeResult.StructSize),
                    OperationStatus,
                    Reserved: 0,
                    required,
                    new AssetCatalogNativeTextSpan(0, (ulong)payload.Length),
                    new AssetCatalogNativeTextSpan((ulong)payload.Length, (ulong)message.Length));
                result = ResultMutation?.Invoke(result) ?? result;
                if (required > responseCapacity)
                {
                    return AssetCatalogNativeStatus.BufferTooSmall;
                }
                if (payload.Length != 0)
                {
                    Marshal.Copy(payload, 0, responseUtf8, payload.Length);
                }
                if (message.Length != 0)
                {
                    Marshal.Copy(message, 0, responseUtf8 + payload.Length, message.Length);
                }
                return CallStatusOverride ?? OperationStatus;
            }
            finally
            {
                Interlocked.Decrement(ref activeCalls_);
            }
        }

        private void UpdateMaximum(int candidate)
        {
            while (true)
            {
                var current = Volatile.Read(ref maximumConcurrentCalls_);
                if (candidate <= current
                    || Interlocked.CompareExchange(
                        ref maximumConcurrentCalls_,
                        candidate,
                        current) == current)
                {
                    return;
                }
            }
        }

        private static string Read(AssetCatalogNativeStringView value)
        {
            var bytes = new byte[checked((int)value.ByteLength)];
            if (bytes.Length != 0)
            {
                Marshal.Copy(value.Data, bytes, 0, bytes.Length);
            }
            return Encoding.UTF8.GetString(bytes);
        }
    }

    private sealed class TrackingArrayPool : ArrayPool<byte>
    {
        public int RentCount { get; private set; }
        public int ReturnCount { get; private set; }
        public int LastMinimumLength { get; private set; }
        public bool LastClearArray { get; private set; }

        public override byte[] Rent(int minimumLength)
        {
            RentCount++;
            LastMinimumLength = minimumLength;
            return new byte[minimumLength];
        }

        public override void Return(byte[] array, bool clearArray = false)
        {
            ArgumentNullException.ThrowIfNull(array);
            ReturnCount++;
            LastClearArray = clearArray;
            if (clearArray)
            {
                Array.Clear(array);
            }
        }
    }
}
