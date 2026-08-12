#pragma once

#include <cstddef>
#include <cstdint>

#include "native_bridge/frame_debugger_native_api.hpp"

extern "C" {

enum EditorViewportNativeStatus : std::uint32_t {
    EditorViewportNativeStatus_Success = 0U,
    EditorViewportNativeStatus_InvalidArgument = 1U,
    EditorViewportNativeStatus_Unavailable = 2U,
    EditorViewportNativeStatus_UnsupportedAbi = 3U,
    EditorViewportNativeStatus_UnsupportedCompositionInterop = 4U,
    EditorViewportNativeStatus_DeviceMismatch = 5U,
    EditorViewportNativeStatus_UnsupportedHandleType = 6U,
    EditorViewportNativeStatus_RenderFailed = 7U,
    EditorViewportNativeStatus_DeviceLost = 8U,
    EditorViewportNativeStatus_InternalError = 9U,
    EditorViewportNativeStatus_Backpressure = 10U,
    EditorViewportNativeStatus_FeatureUnavailable = 11U,
};

enum EditorViewportNativeHandleType : std::uint32_t {
    EditorViewportNativeHandleType_Unknown = 0U,
    EditorViewportNativeHandleType_VulkanOpaqueNt = 1U,
};

enum EditorViewportNativeImageFormat : std::uint32_t {
    EditorViewportNativeImageFormat_Unknown = 0U,
    EditorViewportNativeImageFormat_Rgba8Unorm = 1U,
    EditorViewportNativeImageFormat_Bgra8Unorm = 2U,
};

enum EditorViewportNativeRenderKind : std::uint32_t {
    EditorViewportNativeRenderKind_Scene = 0U,
    EditorViewportNativeRenderKind_Game = 1U,
    EditorViewportNativeRenderKind_Preview = 2U,
};

enum EditorViewportNativeSceneRasterMode : std::uint32_t {
    EditorViewportNativeSceneRasterMode_Solid = 0U,
    EditorViewportNativeSceneRasterMode_Wireframe = 1U,
};

enum EditorViewportNativeFieldOfViewAxis : std::uint32_t {
    EditorViewportNativeFieldOfViewAxis_MaintainHorizontal = 0U,
    EditorViewportNativeFieldOfViewAxis_MaintainVertical = 1U,
};

enum EditorViewportNativeStreamCapabilitiesV7 : std::uint32_t {
    EditorViewportNativeStreamCapabilitiesV7_None = 0U,
    EditorViewportNativeStreamCapabilitiesV7_Wireframe = 1U << 0U,
};

enum EditorViewportNativeTargetKind : std::uint32_t {
    EditorViewportNativeTargetKind_DocumentScene = 0U,
};

enum EditorViewportNativePresentRequestV7Flags : std::uint32_t {
    EditorViewportNativePresentRequestV7Flags_HasLogicalExtent = 1U << 0U,
    EditorViewportNativePresentRequestV7Flags_FlashSentinelCorners = 1U << 1U,
    EditorViewportNativePresentRequestV7Flags_CaptureSceneMeshEvidence = 1U << 2U,
};

enum EditorViewportNativePresentCompletionKind : std::uint32_t {
    EditorViewportNativePresentCompletionKind_NotSubmittedToConsumer = 0U,
    EditorViewportNativePresentCompletionKind_ConsumerAccessed = 1U,
};

enum EditorViewportNativeStreamLifecycle : std::uint32_t {
    EditorViewportNativeStreamLifecycle_Open = 0U,
    EditorViewportNativeStreamLifecycle_Closing = 1U,
    EditorViewportNativeStreamLifecycle_Closed = 2U,
    EditorViewportNativeStreamLifecycle_Faulted = 3U,
};

struct EditorViewportNativeAbiHeader {
    std::uint32_t abiVersion;
    std::uint32_t structSize;
};

struct EditorViewportNativeCompatibilityRequest {
    EditorViewportNativeAbiHeader header;
    std::uint32_t imageHandleType;
    std::uint32_t semaphoreHandleType;
    std::uint64_t deviceLuidLowPart;
    std::int32_t deviceLuidHighPart;
    std::uint32_t hasDeviceLuid;
    std::uint64_t deviceUuidLow;
    std::uint64_t deviceUuidHigh;
    std::uint32_t hasDeviceUuid;
};

struct EditorViewportNativeCompatibilityResult {
    EditorViewportNativeAbiHeader header;
    std::uint32_t status;
    std::uint32_t producedImageHandleType;
    std::uint32_t producedSemaphoreHandleType;
    std::uint32_t nativeDeviceVendorId;
    std::uint32_t nativeDeviceId;
    std::uint64_t nativeDeviceUuidLow;
    std::uint64_t nativeDeviceUuidHigh;
    void* messageUtf8;
    std::uint64_t messageByteLength;
};

struct EditorViewportNativeId {
    std::uint64_t low;
    std::uint64_t high;
};

struct EditorViewportNativeCamera {
    float position[3];
    float target[3];
    float up[3];
    float fieldOfViewRadians;
    std::uint32_t fieldOfViewAxis;
    float nearPlane;
    float farPlane;
};

static_assert(sizeof(EditorViewportNativeFieldOfViewAxis) == 4U);
static_assert(sizeof(EditorViewportNativeCamera) == 52U);
static_assert(offsetof(EditorViewportNativeCamera, fieldOfViewRadians) == 36U);
static_assert(offsetof(EditorViewportNativeCamera, fieldOfViewAxis) == 40U);
static_assert(offsetof(EditorViewportNativeCamera, nearPlane) == 44U);
static_assert(offsetof(EditorViewportNativeCamera, farPlane) == 48U);

struct EditorViewportNativeDebugProxy {
    EditorViewportNativeId objectId;
    float position[3];
    float rotation[4];
    float scale[3];
};

// UUIDs in this ABI use the RFC 4122/network (big-endian) byte order.  They are
// deliberately not EditorViewportNativeId because that legacy representation is
// retained only for session/target echo fields.
struct EditorViewportNativeAuthoredMeshSnapshotV7 {
    std::uint8_t objectId[16];
    std::uint32_t runtimeEntityIndex;
    std::uint32_t runtimeEntityGeneration;
    std::uint8_t assetId[16];
    std::uint64_t expectedMeshType;
    float position[3];
    float rotation[4];
    float scale[3];
};

static_assert(sizeof(EditorViewportNativeAuthoredMeshSnapshotV7) == 88U);
static_assert(offsetof(EditorViewportNativeAuthoredMeshSnapshotV7, objectId) == 0U);
static_assert(offsetof(EditorViewportNativeAuthoredMeshSnapshotV7, runtimeEntityIndex) == 16U);
static_assert(offsetof(EditorViewportNativeAuthoredMeshSnapshotV7, runtimeEntityGeneration) == 20U);
static_assert(offsetof(EditorViewportNativeAuthoredMeshSnapshotV7, assetId) == 24U);
static_assert(offsetof(EditorViewportNativeAuthoredMeshSnapshotV7, expectedMeshType) == 40U);
static_assert(offsetof(EditorViewportNativeAuthoredMeshSnapshotV7, position) == 48U);
static_assert(offsetof(EditorViewportNativeAuthoredMeshSnapshotV7, rotation) == 60U);
static_assert(offsetof(EditorViewportNativeAuthoredMeshSnapshotV7, scale) == 76U);

struct EditorViewportNativeStreamHandleV7 {
    EditorViewportNativeAbiHeader header;
    std::uint32_t status;
    std::uint32_t capabilities;
    std::uint64_t streamId;
};

static_assert(sizeof(EditorViewportNativeStreamHandleV7) == 24U);

struct EditorViewportNativePresentRequestV7 {
    EditorViewportNativeAbiHeader header;
    EditorViewportNativeId sessionId;
    EditorViewportNativeId targetId;
    std::uint64_t targetRevision;
    std::uint64_t requestSequence;
    const EditorViewportNativeDebugProxy* debugProxies;
    std::uint32_t debugProxyCount;
    std::uint32_t kind;
    std::uint32_t targetKind;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t flags;
    EditorViewportNativeCamera camera;
    std::uint32_t logicalWidthPixels;
    std::uint32_t logicalHeightPixels;
    const EditorViewportNativeAuthoredMeshSnapshotV7* authoredMeshes;
    std::uint32_t authoredMeshCount;
    std::uint32_t sceneRasterMode;
};

static_assert(sizeof(EditorViewportNativePresentRequestV7) == 168U);
static_assert(offsetof(EditorViewportNativePresentRequestV7, camera) == 88U);
static_assert(offsetof(EditorViewportNativePresentRequestV7, logicalWidthPixels) == 140U);
static_assert(offsetof(EditorViewportNativePresentRequestV7, logicalHeightPixels) == 144U);
static_assert(offsetof(EditorViewportNativePresentRequestV7, authoredMeshes) == 152U);
static_assert(offsetof(EditorViewportNativePresentRequestV7, authoredMeshCount) == 160U);
static_assert(offsetof(EditorViewportNativePresentRequestV7, sceneRasterMode) == 164U);

struct EditorViewportNativeSceneMeshReceiptV7 {
    std::uint32_t inputCount;
    std::uint32_t resolvedCount;
    std::uint32_t rejectedCount;
    std::uint32_t indexedDrawCount;
    std::uint32_t rasterMode;
    std::uint32_t representativeSourceEntityIndex;
    std::uint32_t representativeSourceEntityGeneration;
    std::uint32_t evidenceAvailable;
    std::uint8_t representativeObjectId[16];
    std::uint8_t representativeAssetId[16];
    std::uint64_t meshResourceKey;
    std::uint64_t materialResourceKey;
    std::uint64_t productHash;
    std::uint64_t sceneRevision;
};

static_assert(sizeof(EditorViewportNativeSceneMeshReceiptV7) == 96U);

struct EditorViewportNativeReadyFrameV7 {
    EditorViewportNativeAbiHeader header;
    std::uint32_t status;
    std::uint32_t hasFrame;
    std::uint64_t streamId;
    void* nativeSlot;
    void* imageHandle;
    void* waitSemaphoreHandle;
    void* signalSemaphoreHandle;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t format;
    std::uint32_t reserved;
    std::uint64_t memorySizeBytes;
    std::uint64_t frameIndex;
    EditorViewportNativeId sessionId;
    EditorViewportNativeId targetId;
    std::uint64_t targetRevision;
    std::uint64_t requestSequence;
    std::uint32_t kind;
    std::uint32_t targetKind;
    std::uint32_t logicalWidthPixels;
    std::uint32_t logicalHeightPixels;
    EditorViewportNativeSceneMeshReceiptV7 sceneMeshReceipt;
};

static_assert(sizeof(EditorViewportNativeReadyFrameV7) == 248U);

struct EditorViewportNativeStreamPollV7 {
    EditorViewportNativeAbiHeader header;
    std::uint32_t status;
    std::uint32_t lifecycle;
    std::uint32_t hasPendingLatest;
    std::uint32_t hasReadyFrame;
    std::uint32_t renderExecuting;
    std::uint32_t slotCount;
    std::uint32_t presentedSlotCount;
    std::uint32_t reserved;
    std::uint64_t submittedRequests;
    std::uint64_t coalescedRequests;
    std::uint64_t renderedFrames;
};

static_assert(sizeof(EditorViewportNativeStreamPollV7) == 64U);

struct EditorViewportNativeRuntimeStats {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};

struct EditorViewportNativeRuntimeStatsV2 {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint64_t externalImagesAcquired;
    std::uint64_t externalImagesCreated;
    std::uint64_t externalImagesReused;
    std::uint64_t externalImagesReleased;
    std::uint64_t externalImagesAvailable;
    std::uint64_t externalImagesLeased;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};

struct EditorViewportNativeRuntimeStatsV3 {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint64_t externalImagesAcquired;
    std::uint64_t externalImagesCreated;
    std::uint64_t externalImagesReused;
    std::uint64_t externalImagesReleased;
    std::uint64_t externalImagesAvailable;
    std::uint64_t externalImagesLeased;
    std::uint64_t frameEpochsSubmitted;
    std::uint64_t frameEpochsCompleted;
    std::uint64_t frameEpochsPending;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};

struct EditorViewportNativeRuntimeStatsV4 {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint64_t externalImagesAcquired;
    std::uint64_t externalImagesCreated;
    std::uint64_t externalImagesReused;
    std::uint64_t externalImagesReleased;
    std::uint64_t externalImagesAvailable;
    std::uint64_t externalImagesLeased;
    std::uint64_t frameEpochsSubmitted;
    std::uint64_t frameEpochsCompleted;
    std::uint64_t frameEpochsPending;
    std::uint64_t rendererCreations;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};

struct EditorViewportNativeRuntimeStatsV5 {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint64_t externalImagesAcquired;
    std::uint64_t externalImagesCreated;
    std::uint64_t externalImagesReused;
    std::uint64_t externalImagesReleased;
    std::uint64_t externalImagesAvailable;
    std::uint64_t externalImagesLeased;
    std::uint64_t frameEpochsSubmitted;
    std::uint64_t frameEpochsCompleted;
    std::uint64_t frameEpochsPending;
    std::uint64_t rendererCreations;
    std::uint64_t maxOutstandingPackets;
    std::uint64_t packetBackpressureHits;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};

struct EditorViewportNativeRuntimeStatsV6 {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint64_t externalImagesAcquired;
    std::uint64_t externalImagesCreated;
    std::uint64_t externalImagesReused;
    std::uint64_t externalImagesReleased;
    std::uint64_t externalImagesAvailable;
    std::uint64_t externalImagesLeased;
    std::uint64_t frameEpochsSubmitted;
    std::uint64_t frameEpochsCompleted;
    std::uint64_t frameEpochsPending;
    std::uint64_t rendererCreations;
    std::uint64_t maxOutstandingPackets;
    std::uint64_t packetBackpressureHits;
    std::uint64_t sceneFramesRendered;
    std::uint64_t lastSceneRevision;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};

struct EditorViewportNativeRuntimeStatsV7 {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint64_t externalImagesAcquired;
    std::uint64_t externalImagesCreated;
    std::uint64_t externalImagesReused;
    std::uint64_t externalImagesReleased;
    std::uint64_t externalImagesAvailable;
    std::uint64_t externalImagesLeased;
    std::uint64_t frameEpochsSubmitted;
    std::uint64_t frameEpochsCompleted;
    std::uint64_t frameEpochsPending;
    std::uint64_t rendererCreations;
    std::uint64_t maxOutstandingPackets;
    std::uint64_t packetBackpressureHits;
    std::uint64_t sceneFramesRendered;
    std::uint64_t gameFramesRendered;
    std::uint64_t previewFramesRendered;
    std::uint64_t lastTargetRevision;
    std::uint64_t lastRequestSequence;
    EditorViewportNativeId lastSessionId;
    EditorViewportNativeId lastTargetId;
    std::uint64_t lastDebugWorldLineCount;
    std::uint32_t lastRenderKind;
    std::uint32_t lastDebugProxyCount;
    std::uint32_t lastWorldGridEnabled;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
};

struct EditorViewportNativeRuntimeStatsV8 {
    EditorViewportNativeAbiHeader header;
    std::uint64_t framesRendered;
    std::uint64_t producersCreated;
    std::uint64_t packetsCreated;
    std::uint64_t outstandingPackets;
    std::uint64_t externalImagesAcquired;
    std::uint64_t externalImagesCreated;
    std::uint64_t externalImagesReused;
    std::uint64_t externalImagesReleased;
    std::uint64_t externalImagesAvailable;
    std::uint64_t externalImagesLeased;
    std::uint64_t frameEpochsSubmitted;
    std::uint64_t frameEpochsCompleted;
    std::uint64_t frameEpochsPending;
    std::uint64_t rendererCreations;
    std::uint64_t maxOutstandingPackets;
    std::uint64_t packetBackpressureHits;
    std::uint64_t sceneFramesRendered;
    std::uint64_t gameFramesRendered;
    std::uint64_t previewFramesRendered;
    std::uint64_t lastTargetRevision;
    std::uint64_t lastRequestSequence;
    EditorViewportNativeId lastSessionId;
    EditorViewportNativeId lastTargetId;
    std::uint64_t lastDebugWorldLineCount;
    std::uint32_t lastRenderKind;
    std::uint32_t lastDebugProxyCount;
    std::uint32_t lastWorldGridEnabled;
    std::uint32_t hasContext;
    std::uint32_t hasRenderProducer;
    std::uint32_t shutdownRequested;
    std::uint32_t lastRenderWidthPixels;
    std::uint32_t lastRenderHeightPixels;
};

static_assert(sizeof(EditorViewportNativeRuntimeStatsV8) == 248U);

enum EditorViewportNativeRuntimeLifecycle : std::uint32_t {
    EditorViewportNativeRuntimeLifecycle_Starting = 0,
    EditorViewportNativeRuntimeLifecycle_Running = 1,
    EditorViewportNativeRuntimeLifecycle_Draining = 2,
    EditorViewportNativeRuntimeLifecycle_Stopped = 3,
    EditorViewportNativeRuntimeLifecycle_Faulted = 4,
};

struct EditorViewportNativeRenderThreadStats {
    EditorViewportNativeAbiHeader header;
    std::uint64_t dispatches;
    std::uint64_t renderQueueBackpressureHits;
    std::uint64_t maxQueuedRenderCommands;
    std::uint64_t maxObservedQueuedRenderCommands;
    std::uint64_t queuedRenderCommands;
    std::uint32_t lifecycle;
    std::uint32_t renderThreadRunning;
    std::uint32_t renderThreadJoined;
    std::uint32_t callerIsRenderThread;
};

static_assert(sizeof(EditorViewportNativeRenderThreadStats) == 64U);
static_assert(offsetof(EditorViewportNativeRenderThreadStats, dispatches) == 8U);
static_assert(offsetof(EditorViewportNativeRenderThreadStats, renderQueueBackpressureHits) == 16U);
static_assert(offsetof(EditorViewportNativeRenderThreadStats, queuedRenderCommands) == 40U);
static_assert(offsetof(EditorViewportNativeRenderThreadStats, lifecycle) == 48U);
static_assert(offsetof(EditorViewportNativeRenderThreadStats, callerIsRenderThread) == 60U);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_viewport_query_composition_compatibility(
    const EditorViewportNativeCompatibilityRequest* request,
    EditorViewportNativeCompatibilityResult* result);

EDITOR_NATIVE_API void EDITOR_NATIVE_CALL
editor_viewport_release_compatibility_result(EditorViewportNativeCompatibilityResult result);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_open_stream_v7(const EditorViewportNativeCompatibilityRequest* compatibility,
                               EditorViewportNativeStreamHandleV7* stream);

#if defined(ASHARIA_EDITOR_NATIVE_TESTING)
EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_viewport_open_stream_v7_for_test(
    const EditorViewportNativeCompatibilityRequest* compatibility, std::uint32_t capabilities,
    EditorViewportNativeStreamHandleV7* stream);
#endif

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_viewport_submit_latest_v7(
    std::uint64_t streamId, const EditorViewportNativePresentRequestV7* request);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_try_take_ready_v7(std::uint64_t streamId, EditorViewportNativeReadyFrameV7* frame);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_viewport_complete_frame_v7(
    std::uint64_t streamId, void* nativeSlot, std::uint32_t completionKind);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_release_slot_import_v7(std::uint64_t streamId, void* nativeSlot);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_close_stream_v7(std::uint64_t streamId);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_poll_stream_v7(std::uint64_t streamId, EditorViewportNativeStreamPollV7* poll);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_destroy_stream_v7(std::uint64_t streamId);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats(EditorViewportNativeRuntimeStats* stats);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v2(EditorViewportNativeRuntimeStatsV2* stats);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v3(EditorViewportNativeRuntimeStatsV3* stats);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v4(EditorViewportNativeRuntimeStatsV4* stats);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v5(EditorViewportNativeRuntimeStatsV5* stats);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v6(EditorViewportNativeRuntimeStatsV6* stats);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v7(EditorViewportNativeRuntimeStatsV7* stats);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v8(EditorViewportNativeRuntimeStatsV8* stats);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_render_thread_stats(EditorViewportNativeRenderThreadStats* stats);

EDITOR_NATIVE_API void EDITOR_NATIVE_CALL editor_viewport_shutdown();

} // extern "C"
