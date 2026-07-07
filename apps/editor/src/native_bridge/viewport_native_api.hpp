#pragma once

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

struct EditorViewportNativePresentPacket {
    EditorViewportNativeAbiHeader header;
    std::uint32_t status;
    void* nativePacket;
    void* imageHandle;
    void* waitSemaphoreHandle;
    void* signalSemaphoreHandle;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    std::uint32_t format;
    std::uint64_t memorySizeBytes;
    std::uint64_t frameIndex;
    void* messageUtf8;
    std::uint64_t messageByteLength;
};

struct EditorViewportNativePresentRequest {
    EditorViewportNativeAbiHeader header;
    EditorViewportNativeCompatibilityRequest compatibility;
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
};

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

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_composition_compatibility(
    const EditorViewportNativeCompatibilityRequest* request,
    EditorViewportNativeCompatibilityResult* result);

EDITOR_NATIVE_API void EDITOR_NATIVE_CALL
editor_viewport_release_compatibility_result(EditorViewportNativeCompatibilityResult result);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL editor_viewport_acquire_present_packet(
    const EditorViewportNativePresentRequest* request,
    EditorViewportNativePresentPacket* packet);

EDITOR_NATIVE_API void EDITOR_NATIVE_CALL
editor_viewport_release_present_packet(EditorViewportNativePresentPacket packet);

EDITOR_NATIVE_API std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats(EditorViewportNativeRuntimeStats* stats);

EDITOR_NATIVE_API void EDITOR_NATIVE_CALL editor_viewport_shutdown();

} // extern "C"
