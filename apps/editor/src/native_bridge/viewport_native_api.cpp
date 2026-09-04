#include "native_bridge/viewport_native_api.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include "asharia/rhi_vulkan/vulkan_context.hpp"

#include "editor_shared_viewport_runtime.hpp"

namespace {

    [[nodiscard]] EditorViewportNativeAbiHeader compatibilityResultHeader() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize =
                static_cast<std::uint32_t>(sizeof(EditorViewportNativeCompatibilityResult)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader streamHandleV8Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeStreamHandleV8)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader readyFrameV8Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeReadyFrameV8)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader streamPollV8Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeStreamPollV8)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsHeader() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStats)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV2Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV2)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV3Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV3)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV4Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV4)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV5Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV5)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV6Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV6)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV7Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV7)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader runtimeStatsV8Header() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRuntimeStatsV8)),
        };
    }

    [[nodiscard]] EditorViewportNativeAbiHeader renderThreadStatsHeader() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativeRenderThreadStats)),
        };
    }

    [[nodiscard]] std::uint32_t
    nativeLifecycleFor(asharia::editor::EditorSharedViewportRuntimeLifecycle lifecycle) {
        switch (lifecycle) {
        case asharia::editor::EditorSharedViewportRuntimeLifecycle::Starting:
            return EditorViewportNativeRuntimeLifecycle_Starting;
        case asharia::editor::EditorSharedViewportRuntimeLifecycle::Running:
            return EditorViewportNativeRuntimeLifecycle_Running;
        case asharia::editor::EditorSharedViewportRuntimeLifecycle::Draining:
            return EditorViewportNativeRuntimeLifecycle_Draining;
        case asharia::editor::EditorSharedViewportRuntimeLifecycle::Stopped:
            return EditorViewportNativeRuntimeLifecycle_Stopped;
        case asharia::editor::EditorSharedViewportRuntimeLifecycle::Faulted:
            return EditorViewportNativeRuntimeLifecycle_Faulted;
        }
        return EditorViewportNativeRuntimeLifecycle_Faulted;
    }

    [[nodiscard]] std::uint32_t
    nativeStreamLifecycleFor(asharia::editor::EditorSharedViewportStreamLifecycle lifecycle) {
        switch (lifecycle) {
        case asharia::editor::EditorSharedViewportStreamLifecycle::Open:
            return EditorViewportNativeStreamLifecycle_Open;
        case asharia::editor::EditorSharedViewportStreamLifecycle::Closing:
            return EditorViewportNativeStreamLifecycle_Closing;
        case asharia::editor::EditorSharedViewportStreamLifecycle::Closed:
            return EditorViewportNativeStreamLifecycle_Closed;
        case asharia::editor::EditorSharedViewportStreamLifecycle::Faulted:
            return EditorViewportNativeStreamLifecycle_Faulted;
        }
        return EditorViewportNativeStreamLifecycle_Faulted;
    }

    [[nodiscard]] bool
    hasSupportedRequestHeader(const EditorViewportNativeCompatibilityRequest& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >= sizeof(EditorViewportNativeCompatibilityRequest);
    }

    [[nodiscard]] bool
    hasSupportedPresentRequestV8Header(const EditorViewportNativePresentRequestV8& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >= sizeof(EditorViewportNativePresentRequestV8);
    }

    [[nodiscard]] asharia::editor::EditorExtent2D
    logicalExtentFor(const EditorViewportNativePresentRequestV8& request) {
        return asharia::editor::EditorExtent2D{
            .width = request.logicalWidthPixels,
            .height = request.logicalHeightPixels,
        };
    }

    [[nodiscard]] bool
    hasSupportedHandleTypes(const EditorViewportNativeCompatibilityRequest& request) {
        return request.imageHandleType == EditorViewportNativeHandleType_VulkanOpaqueNt &&
               request.semaphoreHandleType == EditorViewportNativeHandleType_VulkanOpaqueNt;
    }

    [[nodiscard]] constexpr bool hasValue(EditorViewportNativeId nativeId) {
        return nativeId.low != 0U || nativeId.high != 0U;
    }

    [[nodiscard]] bool finite3(std::span<const float, 3> values) {
        return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]);
    }

    [[nodiscard]] bool validCamera(const EditorViewportNativeCamera& camera) {
        if (!finite3(camera.position) || !finite3(camera.target) || !finite3(camera.up) ||
            !std::isfinite(camera.fieldOfViewRadians) || !std::isfinite(camera.nearPlane) ||
            !std::isfinite(camera.farPlane) || camera.fieldOfViewRadians <= 0.0F ||
            camera.fieldOfViewRadians >= std::numbers::pi_v<float> ||
            (camera.fieldOfViewAxis != EditorViewportNativeFieldOfViewAxis_MaintainHorizontal &&
             camera.fieldOfViewAxis != EditorViewportNativeFieldOfViewAxis_MaintainVertical) ||
            camera.nearPlane <= 0.0F || camera.farPlane <= camera.nearPlane) {
            return false;
        }

        const auto lengthSquared = [](const auto& values) {
            return (values[0] * values[0]) + (values[1] * values[1]) + (values[2] * values[2]);
        };
        const std::array<float, 3> direction{
            camera.target[0] - camera.position[0],
            camera.target[1] - camera.position[1],
            camera.target[2] - camera.position[2],
        };
        constexpr float kMinimumLengthSquared = 1.0e-8F;
        return lengthSquared(direction) > kMinimumLengthSquared &&
               lengthSquared(camera.up) > kMinimumLengthSquared;
    }

    [[nodiscard]] bool validDebugProxy(const EditorViewportNativeDebugProxy& proxy) {
        if (!hasValue(proxy.objectId) || !finite3(proxy.position) || !finite3(proxy.scale)) {
            return false;
        }
        float rotationLengthSquared{};
        for (float value : proxy.rotation) {
            if (!std::isfinite(value)) {
                return false;
            }
            rotationLengthSquared += value * value;
        }
        return std::abs(rotationLengthSquared - 1.0F) <= 1.0e-3F;
    }

    [[nodiscard]] bool validCanonicalUuid(std::span<const std::uint8_t, 16> value) noexcept {
        return std::ranges::any_of(value, [](std::uint8_t byte) { return byte != 0U; });
    }

    [[nodiscard]] EditorViewportNativeId
    nativeIdForCanonicalUuid(std::span<const std::uint8_t, 16> value) noexcept {
        const std::array<std::uint8_t, 16> guidBytes{
            value[3], value[2], value[1], value[0], value[5], value[4], value[7], value[6],
            value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15],
        };
        const auto readLittleEndian = [](std::span<const std::uint8_t, 8> bytes) {
            return static_cast<std::uint64_t>(bytes[0]) |
                   (static_cast<std::uint64_t>(bytes[1]) << 8U) |
                   (static_cast<std::uint64_t>(bytes[2]) << 16U) |
                   (static_cast<std::uint64_t>(bytes[3]) << 24U) |
                   (static_cast<std::uint64_t>(bytes[4]) << 32U) |
                   (static_cast<std::uint64_t>(bytes[5]) << 40U) |
                   (static_cast<std::uint64_t>(bytes[6]) << 48U) |
                   (static_cast<std::uint64_t>(bytes[7]) << 56U);
        };
        return EditorViewportNativeId{
            .low = readLittleEndian(std::span{guidBytes}.first<8>()),
            .high = readLittleEndian(std::span{guidBytes}.last<8>()),
        };
    }

    [[nodiscard]] bool gizmoTargetsSelection(
        const EditorViewportNativeTranslateGizmoV8& gizmo,
        std::span<const std::uint8_t, 16> selectedObjectId) noexcept {
        const EditorViewportNativeId selection = nativeIdForCanonicalUuid(selectedObjectId);
        return gizmo.objectId.low == selection.low && gizmo.objectId.high == selection.high;
    }

    [[nodiscard]] bool
    validAuthoredMesh(const EditorViewportNativeAuthoredMeshSnapshotV8& snapshot) {
        if (!validCanonicalUuid(std::span{snapshot.objectId}) ||
            !validCanonicalUuid(std::span{snapshot.assetId}) || snapshot.runtimeEntityIndex == 0U ||
            snapshot.runtimeEntityGeneration == 0U ||
            snapshot.expectedMeshType != 0x900405520f80e8e6ULL || !finite3(snapshot.position) ||
            !finite3(snapshot.scale)) {
            return false;
        }
        float rotationLengthSquared{};
        for (const float value : snapshot.rotation) {
            if (!std::isfinite(value)) {
                return false;
            }
            rotationLengthSquared += value * value;
        }
        return std::abs(rotationLengthSquared - 1.0e0F) <= 1.0e-3F;
    }

    [[nodiscard]] bool
    isZeroTranslateGizmo(const EditorViewportNativeTranslateGizmoV8& gizmo) noexcept {
        return !hasValue(gizmo.objectId) && gizmo.position[0] == 0.0F &&
               gizmo.position[1] == 0.0F && gizmo.position[2] == 0.0F &&
               gizmo.hoveredAxis == EditorViewportNativeGizmoAxis_None &&
               gizmo.activeAxis == EditorViewportNativeGizmoAxis_None;
    }

    [[nodiscard]] bool
    validTranslateGizmo(const EditorViewportNativeTranslateGizmoV8& gizmo) noexcept {
        return hasValue(gizmo.objectId) && finite3(gizmo.position) &&
               gizmo.hoveredAxis <= EditorViewportNativeGizmoAxis_Z &&
               gizmo.activeAxis <= EditorViewportNativeGizmoAxis_Z;
    }

    [[nodiscard]] bool validPresentRequestV8(const EditorViewportNativePresentRequestV8& request) {
        constexpr std::uint32_t kMaximumDebugProxyCount = 256U;
        constexpr std::uint32_t kMaximumAuthoredMeshCount = 10'000U;
        constexpr std::uint32_t kKnownFlags =
            EditorViewportNativePresentRequestV8Flags_HasLogicalExtent |
            EditorViewportNativePresentRequestV8Flags_FlashSentinelCorners |
            EditorViewportNativePresentRequestV8Flags_CaptureSceneMeshEvidence |
            EditorViewportNativePresentRequestV8Flags_HasSelectionOutline |
            EditorViewportNativePresentRequestV8Flags_HasTranslateGizmo;
        const bool hasSelectionOutline =
            (request.flags & EditorViewportNativePresentRequestV8Flags_HasSelectionOutline) != 0U;
        const bool hasTranslateGizmo =
            (request.flags & EditorViewportNativePresentRequestV8Flags_HasTranslateGizmo) != 0U;
        const asharia::editor::EditorExtent2D logicalExtent = logicalExtentFor(request);
        if (!hasValue(request.sessionId) || !hasValue(request.targetId) ||
            request.targetRevision == 0U || request.requestSequence == 0U ||
            request.widthPixels == 0U || request.heightPixels == 0U ||
            (request.flags & EditorViewportNativePresentRequestV8Flags_HasLogicalExtent) == 0U ||
            (request.flags & ~kKnownFlags) != 0U || logicalExtent.width == 0U ||
            logicalExtent.height == 0U || logicalExtent.width > request.widthPixels ||
            logicalExtent.height > request.heightPixels ||
            request.kind > EditorViewportNativeRenderKind_Preview ||
            request.targetKind != EditorViewportNativeTargetKind_DocumentScene ||
            request.debugProxyCount > kMaximumDebugProxyCount ||
            (request.debugProxyCount != 0U && request.debugProxies == nullptr) ||
            request.authoredMeshCount > kMaximumAuthoredMeshCount ||
            (request.authoredMeshCount != 0U && request.authoredMeshes == nullptr) ||
            request.sceneRasterMode > EditorViewportNativeSceneRasterMode_Wireframe ||
            (hasSelectionOutline &&
             (request.kind != EditorViewportNativeRenderKind_Scene ||
              !validCanonicalUuid(std::span{request.selectedObjectId}))) ||
            (!hasSelectionOutline &&
             validCanonicalUuid(std::span{request.selectedObjectId})) ||
            (hasTranslateGizmo &&
             (request.kind != EditorViewportNativeRenderKind_Scene || !hasSelectionOutline ||
              !validTranslateGizmo(request.translateGizmo) ||
              !gizmoTargetsSelection(request.translateGizmo,
                                     std::span{request.selectedObjectId}))) ||
            (!hasTranslateGizmo && !isZeroTranslateGizmo(request.translateGizmo)) ||
            !validCamera(request.camera)) {
            return false;
        }
        if (request.debugProxyCount != 0U) {
            const std::span<const EditorViewportNativeDebugProxy> debugProxies{
                request.debugProxies, request.debugProxyCount};
            if (!std::ranges::all_of(debugProxies, [](const EditorViewportNativeDebugProxy& proxy) {
                    return validDebugProxy(proxy);
                })) {
                return false;
            }
        }
        if (request.authoredMeshCount == 0U) {
            return true;
        }
        const std::span<const EditorViewportNativeAuthoredMeshSnapshotV8> authoredMeshes{
            request.authoredMeshes, request.authoredMeshCount};
        return std::ranges::all_of(
            authoredMeshes, [](const auto& snapshot) { return validAuthoredMesh(snapshot); });
    }

    [[nodiscard]] bool
    hasUniqueAuthoredMeshIdentitiesV8(const EditorViewportNativePresentRequestV8& request) {
        if (request.authoredMeshCount < 2U) {
            return true;
        }

        std::vector<std::array<std::uint8_t, 16>> objectIds;
        std::vector<std::uint64_t> runtimeEntities;
        objectIds.reserve(request.authoredMeshCount);
        runtimeEntities.reserve(request.authoredMeshCount);
        for (const EditorViewportNativeAuthoredMeshSnapshotV8& mesh :
             std::span{request.authoredMeshes, request.authoredMeshCount}) {
            std::array<std::uint8_t, 16> objectId{};
            std::ranges::copy(mesh.objectId, objectId.begin());
            objectIds.push_back(objectId);
            runtimeEntities.push_back((static_cast<std::uint64_t>(mesh.runtimeEntityIndex) << 32U) |
                                      mesh.runtimeEntityGeneration);
        }

        std::ranges::sort(objectIds);
        std::ranges::sort(runtimeEntities);
        return std::ranges::adjacent_find(objectIds) == objectIds.end() &&
               std::ranges::adjacent_find(runtimeEntities) == runtimeEntities.end();
    }

    [[nodiscard]] asharia::editor::EditorViewportKind viewportKind(std::uint32_t kind) {
        switch (kind) {
        case EditorViewportNativeRenderKind_Scene:
            return asharia::editor::EditorViewportKind::Scene;
        case EditorViewportNativeRenderKind_Game:
            return asharia::editor::EditorViewportKind::Game;
        case EditorViewportNativeRenderKind_Preview:
            return asharia::editor::EditorViewportKind::Preview;
        default:
            return asharia::editor::EditorViewportKind::Scene;
        }
    }

    [[nodiscard]] asharia::editor::EditorViewportCamera
    viewportCamera(const EditorViewportNativeCamera& camera) {
        asharia::editor::EditorViewportCamera result;
        std::ranges::copy(camera.position, result.position.begin());
        std::ranges::copy(camera.target, result.target.begin());
        std::ranges::copy(camera.up, result.up.begin());
        result.fieldOfViewRadians = camera.fieldOfViewRadians;
        result.fieldOfViewAxis =
            camera.fieldOfViewAxis == EditorViewportNativeFieldOfViewAxis_MaintainVertical
                ? asharia::editor::EditorViewportFieldOfViewAxis::MaintainVertical
                : asharia::editor::EditorViewportFieldOfViewAxis::MaintainHorizontal;
        result.nearPlane = camera.nearPlane;
        result.farPlane = camera.farPlane;
        return result;
    }

    void clearCompatibilityResult(EditorViewportNativeCompatibilityResult* result,
                                  std::uint32_t status) {
        if (result == nullptr) {
            return;
        }

        *result = EditorViewportNativeCompatibilityResult{
            .header = compatibilityResultHeader(),
            .status = status,
            .producedImageHandleType = EditorViewportNativeHandleType_Unknown,
            .producedSemaphoreHandleType = EditorViewportNativeHandleType_Unknown,
            .nativeDeviceVendorId = 0U,
            .nativeDeviceId = 0U,
            .nativeDeviceUuidLow = 0U,
            .nativeDeviceUuidHigh = 0U,
            .messageUtf8 = nullptr,
            .messageByteLength = 0U,
        };
    }

    void clearStreamHandleV8(EditorViewportNativeStreamHandleV8* stream, std::uint32_t status) {
        if (stream == nullptr) {
            return;
        }
        *stream = EditorViewportNativeStreamHandleV8{
            .header = streamHandleV8Header(),
            .status = status,
            .capabilities = EditorViewportNativeStreamCapabilitiesV8_None,
            .streamId = 0U,
        };
    }

    void clearReadyFrameV8(EditorViewportNativeReadyFrameV8* frame, std::uint32_t status) {
        if (frame == nullptr) {
            return;
        }
        *frame = {};
        frame->header = readyFrameV8Header();
        frame->status = status;
    }

    void clearStreamPollV8(EditorViewportNativeStreamPollV8* poll, std::uint32_t status) {
        if (poll == nullptr) {
            return;
        }
        *poll = {};
        poll->header = streamPollV8Header();
        poll->status = status;
        poll->lifecycle = EditorViewportNativeStreamLifecycle_Faulted;
    }

    [[nodiscard]] bool allocateMessage(std::string_view message, void*& data,
                                       std::uint64_t& byteLength) {
        data = nullptr;
        byteLength = 0U;
        if (message.empty()) {
            return true;
        }

        // The C ABI returns a native-owned message buffer; callers transfer it
        // back through the matching editor_viewport_release_* function.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
        std::unique_ptr<std::byte[]> buffer;
        try {
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
            buffer = std::make_unique_for_overwrite<std::byte[]>(message.size());
        } catch (const std::bad_alloc&) {
            return false;
        }

        std::memcpy(buffer.get(), message.data(), message.size());
        data = buffer.release();
        byteLength = static_cast<std::uint64_t>(message.size());
        return true;
    }

    [[nodiscard]] std::uint64_t readUuidLow(const std::array<std::uint8_t, VK_UUID_SIZE>& bytes) {
        return static_cast<std::uint64_t>(bytes[0]) | (static_cast<std::uint64_t>(bytes[1]) << 8U) |
               (static_cast<std::uint64_t>(bytes[2]) << 16U) |
               (static_cast<std::uint64_t>(bytes[3]) << 24U) |
               (static_cast<std::uint64_t>(bytes[4]) << 32U) |
               (static_cast<std::uint64_t>(bytes[5]) << 40U) |
               (static_cast<std::uint64_t>(bytes[6]) << 48U) |
               (static_cast<std::uint64_t>(bytes[7]) << 56U);
    }

    [[nodiscard]] std::uint64_t readUuidHigh(const std::array<std::uint8_t, VK_UUID_SIZE>& bytes) {
        return static_cast<std::uint64_t>(bytes[8]) | (static_cast<std::uint64_t>(bytes[9]) << 8U) |
               (static_cast<std::uint64_t>(bytes[10]) << 16U) |
               (static_cast<std::uint64_t>(bytes[11]) << 24U) |
               (static_cast<std::uint64_t>(bytes[12]) << 32U) |
               (static_cast<std::uint64_t>(bytes[13]) << 40U) |
               (static_cast<std::uint64_t>(bytes[14]) << 48U) |
               (static_cast<std::uint64_t>(bytes[15]) << 56U);
    }

    [[nodiscard]] std::uint32_t readLuidLow(const std::array<std::uint8_t, VK_LUID_SIZE>& bytes) {
        return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
               (static_cast<std::uint32_t>(bytes[2]) << 16U) |
               (static_cast<std::uint32_t>(bytes[3]) << 24U);
    }

    [[nodiscard]] std::int32_t readLuidHigh(const std::array<std::uint8_t, VK_LUID_SIZE>& bytes) {
        const std::uint32_t value = static_cast<std::uint32_t>(bytes[4]) |
                                    (static_cast<std::uint32_t>(bytes[5]) << 8U) |
                                    (static_cast<std::uint32_t>(bytes[6]) << 16U) |
                                    (static_cast<std::uint32_t>(bytes[7]) << 24U);
        return std::bit_cast<std::int32_t>(value);
    }

    [[nodiscard]] bool
    matchesRequestedDevice(const EditorViewportNativeCompatibilityRequest& request,
                           const asharia::VulkanDeviceIdentity& identity) {
        if (request.hasDeviceUuid != 0U) {
            const std::uint64_t nativeUuidLow = readUuidLow(identity.deviceUuid);
            const std::uint64_t nativeUuidHigh = readUuidHigh(identity.deviceUuid);
            if (request.deviceUuidLow != nativeUuidLow ||
                request.deviceUuidHigh != nativeUuidHigh) {
                return false;
            }
        }

        if (request.hasDeviceLuid != 0U) {
            if (!identity.deviceLuidValid) {
                return false;
            }

            const std::uint32_t nativeLuidLow = readLuidLow(identity.deviceLuid);
            const std::int32_t nativeLuidHigh = readLuidHigh(identity.deviceLuid);
            if (request.deviceLuidLowPart != nativeLuidLow ||
                request.deviceLuidHighPart != nativeLuidHigh) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::uint32_t writeCompatibilityResult(
        EditorViewportNativeCompatibilityResult* result, std::uint32_t status,
        const asharia::editor::EditorSharedViewportDeviceSnapshot* deviceSnapshot,
        std::string_view message) {
        void* messageData{};
        std::uint64_t messageByteLength{};
        if (!allocateMessage(message, messageData, messageByteLength)) {
            clearCompatibilityResult(result, EditorViewportNativeStatus_InternalError);
            return EditorViewportNativeStatus_InternalError;
        }

        *result = EditorViewportNativeCompatibilityResult{
            .header = compatibilityResultHeader(),
            .status = status,
            .producedImageHandleType = status == EditorViewportNativeStatus_Success
                                           ? EditorViewportNativeHandleType_VulkanOpaqueNt
                                           : EditorViewportNativeHandleType_Unknown,
            .producedSemaphoreHandleType = status == EditorViewportNativeStatus_Success
                                               ? EditorViewportNativeHandleType_VulkanOpaqueNt
                                               : EditorViewportNativeHandleType_Unknown,
            .nativeDeviceVendorId = deviceSnapshot != nullptr ? deviceSnapshot->vendorId : 0U,
            .nativeDeviceId = deviceSnapshot != nullptr ? deviceSnapshot->deviceId : 0U,
            .nativeDeviceUuidLow =
                deviceSnapshot != nullptr ? readUuidLow(deviceSnapshot->identity.deviceUuid) : 0U,
            .nativeDeviceUuidHigh =
                deviceSnapshot != nullptr ? readUuidHigh(deviceSnapshot->identity.deviceUuid) : 0U,
            .messageUtf8 = messageData,
            .messageByteLength = messageByteLength,
        };
        return status;
    }

    [[nodiscard]] std::uint32_t
    submitStreamFrameV8(std::uint64_t streamId,
                        const EditorViewportNativePresentRequestV8& request) {
        const asharia::editor::EditorSharedViewportSceneRasterMode rasterMode =
            request.sceneRasterMode == EditorViewportNativeSceneRasterMode_Wireframe
                ? asharia::editor::EditorSharedViewportSceneRasterMode::Wireframe
                : asharia::editor::EditorSharedViewportSceneRasterMode::Solid;
        auto rasterModeValid =
            asharia::editor::EditorSharedViewportRuntime::instance().validateSceneRasterMode(
                streamId, rasterMode);
        if (!rasterModeValid) {
            if (rasterModeValid.error().domain == asharia::ErrorDomain::Vulkan &&
                rasterModeValid.error().code == static_cast<int>(VK_ERROR_FEATURE_NOT_PRESENT)) {
                return EditorViewportNativeStatus_FeatureUnavailable;
            }
            return EditorViewportNativeStatus_Unavailable;
        }

        std::vector<asharia::editor::EditorSharedViewportDebugProxy> debugProxies;
        std::vector<asharia::editor::EditorSharedViewportAuthoredMeshSnapshot> authoredMeshes;
        try {
            debugProxies.reserve(request.debugProxyCount);
            if (request.debugProxyCount != 0U) {
                const std::span<const EditorViewportNativeDebugProxy> requestProxies{
                    request.debugProxies, request.debugProxyCount};
                for (const EditorViewportNativeDebugProxy& proxy : requestProxies) {
                    debugProxies.push_back(asharia::editor::EditorSharedViewportDebugProxy{
                        .objectId = {proxy.objectId.low, proxy.objectId.high},
                        .position = {proxy.position[0], proxy.position[1], proxy.position[2]},
                        .rotation = {proxy.rotation[0], proxy.rotation[1], proxy.rotation[2],
                                     proxy.rotation[3]},
                        .scale = {proxy.scale[0], proxy.scale[1], proxy.scale[2]},
                    });
                }
            }
            authoredMeshes.reserve(request.authoredMeshCount);
            if (request.authoredMeshCount != 0U) {
                const std::span<const EditorViewportNativeAuthoredMeshSnapshotV8> requestMeshes{
                    request.authoredMeshes, request.authoredMeshCount};
                for (const EditorViewportNativeAuthoredMeshSnapshotV8& mesh : requestMeshes) {
                    asharia::editor::EditorSharedViewportAuthoredMeshSnapshot copy;
                    std::ranges::copy(mesh.objectId, copy.objectId.begin());
                    copy.runtimeEntityIndex = mesh.runtimeEntityIndex;
                    copy.runtimeEntityGeneration = mesh.runtimeEntityGeneration;
                    std::ranges::copy(mesh.assetId, copy.assetId.begin());
                    copy.expectedMeshType = mesh.expectedMeshType;
                    std::ranges::copy(mesh.position, copy.position.begin());
                    std::ranges::copy(mesh.rotation, copy.rotation.begin());
                    std::ranges::copy(mesh.scale, copy.scale.begin());
                    authoredMeshes.push_back(copy);
                }
            }
        } catch (const std::bad_alloc&) {
            return EditorViewportNativeStatus_InternalError;
        }

        std::array<std::uint8_t, 16> selectedObjectId{};
        std::ranges::copy(request.selectedObjectId, selectedObjectId.begin());
        const asharia::editor::EditorSharedViewportPresentDesc desc{
            .panelId = "viewport-stream/native-v8",
            .kind = viewportKind(request.kind),
            .logicalExtent = logicalExtentFor(request),
            .allocationExtent =
                asharia::editor::EditorExtent2D{
                    .width = request.widthPixels,
                    .height = request.heightPixels,
                },
            .hasScene = true,
            .sceneRevision = request.targetRevision,
            .sessionId = {request.sessionId.low, request.sessionId.high},
            .targetId = {request.targetId.low, request.targetId.high},
            .requestSequence = request.requestSequence,
            .viewStateRevision = request.viewStateRevision,
            .hasCamera = true,
            .camera = viewportCamera(request.camera),
            .debugProxies = debugProxies,
            .authoredMeshes = authoredMeshes,
            .sceneRasterMode = rasterMode,
            .captureSceneMeshEvidence =
                (request.flags &
                 EditorViewportNativePresentRequestV8Flags_CaptureSceneMeshEvidence) != 0U,
            .flashSentinelCorners =
                (request.flags & EditorViewportNativePresentRequestV8Flags_FlashSentinelCorners) !=
                0U,
            .hasSelectionOutline =
                (request.flags & EditorViewportNativePresentRequestV8Flags_HasSelectionOutline) !=
                0U,
            .selectedObjectId = selectedObjectId,
            .hasTranslateGizmo =
                (request.flags & EditorViewportNativePresentRequestV8Flags_HasTranslateGizmo) !=
                0U,
            .translateGizmoObjectId = {request.translateGizmo.objectId.low,
                                       request.translateGizmo.objectId.high},
            .translateGizmoPosition = {request.translateGizmo.position[0],
                                       request.translateGizmo.position[1],
                                       request.translateGizmo.position[2]},
            .translateGizmoHoveredAxis =
                static_cast<asharia::editor::EditorSharedViewportGizmoAxis>(
                    request.translateGizmo.hoveredAxis),
            .translateGizmoActiveAxis =
                static_cast<asharia::editor::EditorSharedViewportGizmoAxis>(
                    request.translateGizmo.activeAxis),
        };
        auto submitted =
            asharia::editor::EditorSharedViewportRuntime::instance().submitLatest(streamId, desc);
        if (submitted) {
            return EditorViewportNativeStatus_Success;
        }
        if (submitted.error().domain == asharia::ErrorDomain::Vulkan &&
            submitted.error().code == static_cast<int>(VK_ERROR_FEATURE_NOT_PRESENT)) {
            return EditorViewportNativeStatus_FeatureUnavailable;
        }
        return EditorViewportNativeStatus_Unavailable;
    }

    [[nodiscard]] std::uint32_t
    openStreamV8(const EditorViewportNativeCompatibilityRequest* compatibility,
                 EditorViewportNativeStreamHandleV8* stream,
                 std::optional<bool> wireframeCapabilityOverride = std::nullopt) {
        if (compatibility == nullptr || stream == nullptr) {
            clearStreamHandleV8(stream, EditorViewportNativeStatus_InvalidArgument);
            return EditorViewportNativeStatus_InvalidArgument;
        }
        if (!hasSupportedRequestHeader(*compatibility)) {
            clearStreamHandleV8(stream, EditorViewportNativeStatus_UnsupportedAbi);
            return EditorViewportNativeStatus_UnsupportedAbi;
        }
        if (!hasSupportedHandleTypes(*compatibility)) {
            clearStreamHandleV8(stream, EditorViewportNativeStatus_UnsupportedHandleType);
            return EditorViewportNativeStatus_UnsupportedHandleType;
        }

        try {
            const auto deviceSnapshot =
                asharia::editor::EditorSharedViewportRuntime::instance().ensureDeviceSnapshot();
            if (!deviceSnapshot) {
                clearStreamHandleV8(stream, EditorViewportNativeStatus_Unavailable);
                return EditorViewportNativeStatus_Unavailable;
            }
            if (!matchesRequestedDevice(*compatibility, deviceSnapshot->identity)) {
                clearStreamHandleV8(stream, EditorViewportNativeStatus_DeviceMismatch);
                return EditorViewportNativeStatus_DeviceMismatch;
            }

            const bool supportsWireframe =
                wireframeCapabilityOverride.value_or(deviceSnapshot->fillModeNonSolid);
            auto opened = asharia::editor::EditorSharedViewportRuntime::instance().openStream(
                supportsWireframe);
            if (!opened) {
                clearStreamHandleV8(stream, EditorViewportNativeStatus_Unavailable);
                return EditorViewportNativeStatus_Unavailable;
            }
            *stream = EditorViewportNativeStreamHandleV8{
                .header = streamHandleV8Header(),
                .status = EditorViewportNativeStatus_Success,
                .capabilities = supportsWireframe
                                    ? EditorViewportNativeStreamCapabilitiesV8_Wireframe
                                    : EditorViewportNativeStreamCapabilitiesV8_None,
                .streamId = *opened,
            };
            return EditorViewportNativeStatus_Success;
        } catch (...) {
            clearStreamHandleV8(stream, EditorViewportNativeStatus_InternalError);
            return EditorViewportNativeStatus_InternalError;
        }
    }

} // namespace

extern "C" {

std::uint32_t EDITOR_NATIVE_CALL editor_viewport_query_composition_compatibility(
    const EditorViewportNativeCompatibilityRequest* request,
    EditorViewportNativeCompatibilityResult* result) {
    if (request == nullptr || result == nullptr) {
        clearCompatibilityResult(result, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }

    if (!hasSupportedRequestHeader(*request)) {
        clearCompatibilityResult(result, EditorViewportNativeStatus_UnsupportedAbi);
        return EditorViewportNativeStatus_UnsupportedAbi;
    }

    if (!hasSupportedHandleTypes(*request)) {
        return writeCompatibilityResult(
            result, EditorViewportNativeStatus_UnsupportedHandleType, nullptr,
            "Vulkan opaque NT image and semaphore handles are required.");
    }

    const auto deviceSnapshot =
        asharia::editor::EditorSharedViewportRuntime::instance().ensureDeviceSnapshot();
    if (!deviceSnapshot) {
        return writeCompatibilityResult(result, EditorViewportNativeStatus_Unavailable, nullptr,
                                        deviceSnapshot.error().message);
    }

    if (!matchesRequestedDevice(*request, deviceSnapshot->identity)) {
        return writeCompatibilityResult(
            result, EditorViewportNativeStatus_DeviceMismatch, &*deviceSnapshot,
            "Avalonia compositor device does not match the Vulkan viewport device.");
    }

    return writeCompatibilityResult(
        result, EditorViewportNativeStatus_Success, &*deviceSnapshot,
        "Vulkan viewport device is compatible with Avalonia composition.");
}

void EDITOR_NATIVE_CALL
editor_viewport_release_compatibility_result(EditorViewportNativeCompatibilityResult result) {
    const std::unique_ptr<std::byte[]> message{static_cast<std::byte*>(result.messageUtf8)};
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_open_stream_v8(const EditorViewportNativeCompatibilityRequest* compatibility,
                               EditorViewportNativeStreamHandleV8* stream) {
    return openStreamV8(compatibility, stream);
}

#if defined(ASHARIA_EDITOR_NATIVE_TESTING)
std::uint32_t EDITOR_NATIVE_CALL editor_viewport_open_stream_v8_for_test(
    const EditorViewportNativeCompatibilityRequest* compatibility, std::uint32_t capabilities,
    EditorViewportNativeStreamHandleV8* stream) {
    constexpr std::uint32_t kKnownCapabilities = EditorViewportNativeStreamCapabilitiesV8_Wireframe;
    if ((capabilities & ~kKnownCapabilities) != 0U) {
        clearStreamHandleV8(stream, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }
    return openStreamV8(compatibility, stream,
                        (capabilities & EditorViewportNativeStreamCapabilitiesV8_Wireframe) != 0U);
}
#endif

std::uint32_t EDITOR_NATIVE_CALL editor_viewport_submit_latest_v8(
    std::uint64_t streamId, const EditorViewportNativePresentRequestV8* request) {
    if (streamId == 0U || request == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }
    if (!hasSupportedPresentRequestV8Header(*request)) {
        return EditorViewportNativeStatus_UnsupportedAbi;
    }
    if (!validPresentRequestV8(*request)) {
        return EditorViewportNativeStatus_InvalidArgument;
    }
    try {
        if (!hasUniqueAuthoredMeshIdentitiesV8(*request)) {
            return EditorViewportNativeStatus_InvalidArgument;
        }
        return submitStreamFrameV8(streamId, *request);
    } catch (...) {
        return EditorViewportNativeStatus_InternalError;
    }
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_try_take_ready_v8(std::uint64_t streamId, EditorViewportNativeReadyFrameV8* frame) {
    if (streamId == 0U || frame == nullptr) {
        clearReadyFrameV8(frame, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }

    try {
        auto ready =
            asharia::editor::EditorSharedViewportRuntime::instance().tryTakeReady(streamId);
        if (!ready) {
            clearReadyFrameV8(frame, EditorViewportNativeStatus_Unavailable);
            return EditorViewportNativeStatus_Unavailable;
        }
        if (!*ready) {
            clearReadyFrameV8(frame, EditorViewportNativeStatus_Success);
            return EditorViewportNativeStatus_Success;
        }

        const asharia::editor::EditorSharedViewportReadyFrame& nativeFrame = **ready;
        std::uint32_t format = EditorViewportNativeImageFormat_Unknown;
        if (nativeFrame.present.format == VK_FORMAT_R8G8B8A8_UNORM) {
            format = EditorViewportNativeImageFormat_Rgba8Unorm;
        } else if (nativeFrame.present.format == VK_FORMAT_B8G8R8A8_UNORM) {
            format = EditorViewportNativeImageFormat_Bgra8Unorm;
        }
        if (format == EditorViewportNativeImageFormat_Unknown) {
            [[maybe_unused]] auto completed =
                asharia::editor::EditorSharedViewportRuntime::instance().completeFrame(
                    streamId, nativeFrame.present.nativePacket,
                    asharia::editor::EditorSharedViewportPresentCompletionKind::
                        NotSubmittedToConsumer);
            clearReadyFrameV8(frame, EditorViewportNativeStatus_RenderFailed);
            return EditorViewportNativeStatus_RenderFailed;
        }

        *frame = EditorViewportNativeReadyFrameV8{
            .header = readyFrameV8Header(),
            .status = EditorViewportNativeStatus_Success,
            .hasFrame = 1U,
            .streamId = streamId,
            .nativeSlot = nativeFrame.present.nativePacket,
            .imageHandle = nativeFrame.present.imageHandle,
            .waitSemaphoreHandle = nativeFrame.present.waitSemaphoreHandle,
            .signalSemaphoreHandle = nativeFrame.present.signalSemaphoreHandle,
            .widthPixels = nativeFrame.present.allocationExtent.width,
            .heightPixels = nativeFrame.present.allocationExtent.height,
            .format = format,
            .reserved = 0U,
            .memorySizeBytes = nativeFrame.present.memorySizeBytes,
            .frameIndex = nativeFrame.present.frameIndex,
            .sessionId =
                EditorViewportNativeId{
                    .low = nativeFrame.sessionId[0],
                    .high = nativeFrame.sessionId[1],
                },
            .targetId =
                EditorViewportNativeId{
                    .low = nativeFrame.targetId[0],
                    .high = nativeFrame.targetId[1],
                },
            .targetRevision = nativeFrame.targetRevision,
            .requestSequence = nativeFrame.requestSequence,
            .kind = static_cast<std::uint32_t>(nativeFrame.kind),
            .targetKind = EditorViewportNativeTargetKind_DocumentScene,
            .logicalWidthPixels = nativeFrame.logicalExtent.width,
            .logicalHeightPixels = nativeFrame.logicalExtent.height,
            .sceneMeshReceipt =
                EditorViewportNativeSceneMeshReceiptV8{
                    .inputCount = nativeFrame.sceneMeshReceipt.inputCount,
                    .resolvedCount = nativeFrame.sceneMeshReceipt.resolvedCount,
                    .rejectedCount = nativeFrame.sceneMeshReceipt.rejectedCount,
                    .indexedDrawCount = nativeFrame.sceneMeshReceipt.indexedDrawCount,
                    .rasterMode =
                        static_cast<std::uint32_t>(nativeFrame.sceneMeshReceipt.rasterMode),
                    .representativeSourceEntityIndex =
                        nativeFrame.sceneMeshReceipt.representativeSourceEntityIndex,
                    .representativeSourceEntityGeneration =
                        nativeFrame.sceneMeshReceipt.representativeSourceEntityGeneration,
                    .evidenceAvailable = nativeFrame.sceneMeshReceipt.evidenceAvailable ? 1U : 0U,
                    .representativeObjectId = {},
                    .representativeAssetId = {},
                    .meshResourceKey = nativeFrame.sceneMeshReceipt.meshResourceKey,
                    .materialResourceKey = nativeFrame.sceneMeshReceipt.materialResourceKey,
                    .productHash = nativeFrame.sceneMeshReceipt.productHash,
                    .sceneRevision = nativeFrame.sceneMeshReceipt.sceneRevision,
                },
            .viewStateRevision = nativeFrame.viewStateRevision,
        };
        std::ranges::copy(nativeFrame.sceneMeshReceipt.representativeObjectId,
                          frame->sceneMeshReceipt.representativeObjectId);
        std::ranges::copy(nativeFrame.sceneMeshReceipt.representativeAssetId,
                          frame->sceneMeshReceipt.representativeAssetId);
        return EditorViewportNativeStatus_Success;
    } catch (...) {
        clearReadyFrameV8(frame, EditorViewportNativeStatus_InternalError);
        return EditorViewportNativeStatus_InternalError;
    }
}

std::uint32_t EDITOR_NATIVE_CALL editor_viewport_complete_frame_v8(std::uint64_t streamId,
                                                                   void* nativeSlot,
                                                                   std::uint32_t completionKind) {
    asharia::editor::EditorSharedViewportPresentCompletionKind nativeCompletionKind{};
    switch (completionKind) {
    case EditorViewportNativePresentCompletionKind_NotSubmittedToConsumer:
        nativeCompletionKind =
            asharia::editor::EditorSharedViewportPresentCompletionKind::NotSubmittedToConsumer;
        break;
    case EditorViewportNativePresentCompletionKind_ConsumerAccessed:
        nativeCompletionKind =
            asharia::editor::EditorSharedViewportPresentCompletionKind::ConsumerAccessed;
        break;
    default:
        return EditorViewportNativeStatus_InvalidArgument;
    }
    try {
        auto completed = asharia::editor::EditorSharedViewportRuntime::instance().completeFrame(
            streamId, nativeSlot, nativeCompletionKind);
        return completed ? EditorViewportNativeStatus_Success
                         : EditorViewportNativeStatus_InvalidArgument;
    } catch (...) {
        return EditorViewportNativeStatus_InternalError;
    }
}

std::uint32_t EDITOR_NATIVE_CALL editor_viewport_release_slot_import_v8(std::uint64_t streamId,
                                                                        void* nativeSlot) {
    try {
        auto released = asharia::editor::EditorSharedViewportRuntime::instance().releaseSlotImport(
            streamId, nativeSlot);
        return released ? EditorViewportNativeStatus_Success
                        : EditorViewportNativeStatus_InvalidArgument;
    } catch (...) {
        return EditorViewportNativeStatus_InternalError;
    }
}

std::uint32_t EDITOR_NATIVE_CALL editor_viewport_close_stream_v8(std::uint64_t streamId) {
    try {
        auto closed =
            asharia::editor::EditorSharedViewportRuntime::instance().requestCloseStream(streamId);
        return closed ? EditorViewportNativeStatus_Success
                      : EditorViewportNativeStatus_InvalidArgument;
    } catch (...) {
        return EditorViewportNativeStatus_InternalError;
    }
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_poll_stream_v8(std::uint64_t streamId, EditorViewportNativeStreamPollV8* poll) {
    if (streamId == 0U || poll == nullptr) {
        clearStreamPollV8(poll, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }
    try {
        auto snapshot =
            asharia::editor::EditorSharedViewportRuntime::instance().pollStream(streamId);
        if (!snapshot) {
            clearStreamPollV8(poll, EditorViewportNativeStatus_Unavailable);
            return EditorViewportNativeStatus_Unavailable;
        }
        *poll = EditorViewportNativeStreamPollV8{
            .header = streamPollV8Header(),
            .status = EditorViewportNativeStatus_Success,
            .lifecycle = nativeStreamLifecycleFor(snapshot->lifecycle),
            .hasPendingLatest = snapshot->hasPendingLatest ? 1U : 0U,
            .hasReadyFrame = snapshot->hasReadyFrame ? 1U : 0U,
            .renderExecuting = snapshot->renderExecuting ? 1U : 0U,
            .slotCount = static_cast<std::uint32_t>(snapshot->slotCount),
            .presentedSlotCount = static_cast<std::uint32_t>(snapshot->presentedSlotCount),
            .reserved = 0U,
            .submittedRequests = snapshot->submittedRequests,
            .coalescedRequests = snapshot->coalescedRequests,
            .renderedFrames = snapshot->renderedFrames,
        };
        return EditorViewportNativeStatus_Success;
    } catch (...) {
        clearStreamPollV8(poll, EditorViewportNativeStatus_InternalError);
        return EditorViewportNativeStatus_InternalError;
    }
}

std::uint32_t EDITOR_NATIVE_CALL editor_viewport_destroy_stream_v8(std::uint64_t streamId) {
    try {
        auto destroyed =
            asharia::editor::EditorSharedViewportRuntime::instance().destroyClosedStream(streamId);
        return destroyed ? EditorViewportNativeStatus_Success
                         : EditorViewportNativeStatus_InvalidArgument;
    } catch (...) {
        return EditorViewportNativeStatus_InternalError;
    }
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats(EditorViewportNativeRuntimeStats* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStats{
        .header = runtimeStatsHeader(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v2(EditorViewportNativeRuntimeStatsV2* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV2{
        .header = runtimeStatsV2Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = runtimeStats.externalImagesAcquired,
        .externalImagesCreated = runtimeStats.externalImagesCreated,
        .externalImagesReused = runtimeStats.externalImagesReused,
        .externalImagesReleased = runtimeStats.externalImagesReleased,
        .externalImagesAvailable = runtimeStats.externalImagesAvailable,
        .externalImagesLeased = runtimeStats.externalImagesLeased,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v3(EditorViewportNativeRuntimeStatsV3* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV3{
        .header = runtimeStatsV3Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = runtimeStats.externalImagesAcquired,
        .externalImagesCreated = runtimeStats.externalImagesCreated,
        .externalImagesReused = runtimeStats.externalImagesReused,
        .externalImagesReleased = runtimeStats.externalImagesReleased,
        .externalImagesAvailable = runtimeStats.externalImagesAvailable,
        .externalImagesLeased = runtimeStats.externalImagesLeased,
        .frameEpochsSubmitted = runtimeStats.frameEpochsSubmitted,
        .frameEpochsCompleted = runtimeStats.frameEpochsCompleted,
        .frameEpochsPending = runtimeStats.frameEpochsPending,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v4(EditorViewportNativeRuntimeStatsV4* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV4{
        .header = runtimeStatsV4Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = runtimeStats.externalImagesAcquired,
        .externalImagesCreated = runtimeStats.externalImagesCreated,
        .externalImagesReused = runtimeStats.externalImagesReused,
        .externalImagesReleased = runtimeStats.externalImagesReleased,
        .externalImagesAvailable = runtimeStats.externalImagesAvailable,
        .externalImagesLeased = runtimeStats.externalImagesLeased,
        .frameEpochsSubmitted = runtimeStats.frameEpochsSubmitted,
        .frameEpochsCompleted = runtimeStats.frameEpochsCompleted,
        .frameEpochsPending = runtimeStats.frameEpochsPending,
        .rendererCreations = runtimeStats.rendererCreations,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v5(EditorViewportNativeRuntimeStatsV5* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV5{
        .header = runtimeStatsV5Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = runtimeStats.externalImagesAcquired,
        .externalImagesCreated = runtimeStats.externalImagesCreated,
        .externalImagesReused = runtimeStats.externalImagesReused,
        .externalImagesReleased = runtimeStats.externalImagesReleased,
        .externalImagesAvailable = runtimeStats.externalImagesAvailable,
        .externalImagesLeased = runtimeStats.externalImagesLeased,
        .frameEpochsSubmitted = runtimeStats.frameEpochsSubmitted,
        .frameEpochsCompleted = runtimeStats.frameEpochsCompleted,
        .frameEpochsPending = runtimeStats.frameEpochsPending,
        .rendererCreations = runtimeStats.rendererCreations,
        .maxOutstandingPackets = static_cast<std::uint64_t>(runtimeStats.maxOutstandingPackets),
        .packetBackpressureHits = runtimeStats.packetBackpressureHits,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v6(EditorViewportNativeRuntimeStatsV6* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV6{
        .header = runtimeStatsV6Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = runtimeStats.externalImagesAcquired,
        .externalImagesCreated = runtimeStats.externalImagesCreated,
        .externalImagesReused = runtimeStats.externalImagesReused,
        .externalImagesReleased = runtimeStats.externalImagesReleased,
        .externalImagesAvailable = runtimeStats.externalImagesAvailable,
        .externalImagesLeased = runtimeStats.externalImagesLeased,
        .frameEpochsSubmitted = runtimeStats.frameEpochsSubmitted,
        .frameEpochsCompleted = runtimeStats.frameEpochsCompleted,
        .frameEpochsPending = runtimeStats.frameEpochsPending,
        .rendererCreations = runtimeStats.rendererCreations,
        .maxOutstandingPackets = static_cast<std::uint64_t>(runtimeStats.maxOutstandingPackets),
        .packetBackpressureHits = runtimeStats.packetBackpressureHits,
        .sceneFramesRendered = runtimeStats.sceneFramesRendered,
        .lastSceneRevision = runtimeStats.lastSceneRevision,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v7(EditorViewportNativeRuntimeStatsV7* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV7{
        .header = runtimeStatsV7Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = runtimeStats.externalImagesAcquired,
        .externalImagesCreated = runtimeStats.externalImagesCreated,
        .externalImagesReused = runtimeStats.externalImagesReused,
        .externalImagesReleased = runtimeStats.externalImagesReleased,
        .externalImagesAvailable = runtimeStats.externalImagesAvailable,
        .externalImagesLeased = runtimeStats.externalImagesLeased,
        .frameEpochsSubmitted = runtimeStats.frameEpochsSubmitted,
        .frameEpochsCompleted = runtimeStats.frameEpochsCompleted,
        .frameEpochsPending = runtimeStats.frameEpochsPending,
        .rendererCreations = runtimeStats.rendererCreations,
        .maxOutstandingPackets = static_cast<std::uint64_t>(runtimeStats.maxOutstandingPackets),
        .packetBackpressureHits = runtimeStats.packetBackpressureHits,
        .sceneFramesRendered = runtimeStats.sceneFramesRendered,
        .gameFramesRendered = runtimeStats.gameFramesRendered,
        .previewFramesRendered = runtimeStats.previewFramesRendered,
        .lastTargetRevision = runtimeStats.lastSceneRevision,
        .lastRequestSequence = runtimeStats.lastRequestSequence,
        .lastSessionId =
            EditorViewportNativeId{
                .low = runtimeStats.lastSessionId[0],
                .high = runtimeStats.lastSessionId[1],
            },
        .lastTargetId =
            EditorViewportNativeId{
                .low = runtimeStats.lastTargetId[0],
                .high = runtimeStats.lastTargetId[1],
            },
        .lastDebugWorldLineCount = runtimeStats.lastDebugWorldLineCount,
        .lastRenderKind = static_cast<std::uint32_t>(runtimeStats.lastRenderKind),
        .lastDebugProxyCount = runtimeStats.lastDebugProxyCount,
        .lastWorldGridEnabled = runtimeStats.lastWorldGridEnabled ? 1U : 0U,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_runtime_stats_v8(EditorViewportNativeRuntimeStatsV8* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRuntimeStatsV8{
        .header = runtimeStatsV8Header(),
        .framesRendered = runtimeStats.framesRendered,
        .producersCreated = runtimeStats.producersCreated,
        .packetsCreated = runtimeStats.packetsCreated,
        .outstandingPackets = static_cast<std::uint64_t>(runtimeStats.outstandingPackets),
        .externalImagesAcquired = runtimeStats.externalImagesAcquired,
        .externalImagesCreated = runtimeStats.externalImagesCreated,
        .externalImagesReused = runtimeStats.externalImagesReused,
        .externalImagesReleased = runtimeStats.externalImagesReleased,
        .externalImagesAvailable = runtimeStats.externalImagesAvailable,
        .externalImagesLeased = runtimeStats.externalImagesLeased,
        .frameEpochsSubmitted = runtimeStats.frameEpochsSubmitted,
        .frameEpochsCompleted = runtimeStats.frameEpochsCompleted,
        .frameEpochsPending = runtimeStats.frameEpochsPending,
        .rendererCreations = runtimeStats.rendererCreations,
        .maxOutstandingPackets = static_cast<std::uint64_t>(runtimeStats.maxOutstandingPackets),
        .packetBackpressureHits = runtimeStats.packetBackpressureHits,
        .sceneFramesRendered = runtimeStats.sceneFramesRendered,
        .gameFramesRendered = runtimeStats.gameFramesRendered,
        .previewFramesRendered = runtimeStats.previewFramesRendered,
        .lastTargetRevision = runtimeStats.lastSceneRevision,
        .lastRequestSequence = runtimeStats.lastRequestSequence,
        .lastSessionId =
            EditorViewportNativeId{
                .low = runtimeStats.lastSessionId[0],
                .high = runtimeStats.lastSessionId[1],
            },
        .lastTargetId =
            EditorViewportNativeId{
                .low = runtimeStats.lastTargetId[0],
                .high = runtimeStats.lastTargetId[1],
            },
        .lastDebugWorldLineCount = runtimeStats.lastDebugWorldLineCount,
        .lastRenderKind = static_cast<std::uint32_t>(runtimeStats.lastRenderKind),
        .lastDebugProxyCount = runtimeStats.lastDebugProxyCount,
        .lastWorldGridEnabled = runtimeStats.lastWorldGridEnabled ? 1U : 0U,
        .hasContext = runtimeStats.hasContext ? 1U : 0U,
        .hasRenderProducer = runtimeStats.hasRenderProducer ? 1U : 0U,
        .shutdownRequested = runtimeStats.shutdownRequested ? 1U : 0U,
        .lastRenderWidthPixels = runtimeStats.lastRenderExtent.width,
        .lastRenderHeightPixels = runtimeStats.lastRenderExtent.height,
    };
    return EditorViewportNativeStatus_Success;
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_query_render_thread_stats(EditorViewportNativeRenderThreadStats* stats) {
    if (stats == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    const asharia::editor::EditorSharedViewportRuntimeStats runtimeStats =
        asharia::editor::EditorSharedViewportRuntime::instance().stats();
    *stats = EditorViewportNativeRenderThreadStats{
        .header = renderThreadStatsHeader(),
        .dispatches = runtimeStats.renderThreadDispatches,
        .renderQueueBackpressureHits = runtimeStats.renderQueueBackpressureHits,
        .maxQueuedRenderCommands = static_cast<std::uint64_t>(runtimeStats.maxQueuedRenderCommands),
        .maxObservedQueuedRenderCommands =
            static_cast<std::uint64_t>(runtimeStats.maxObservedQueuedRenderCommands),
        .queuedRenderCommands = static_cast<std::uint64_t>(runtimeStats.queuedRenderCommands),
        .lifecycle = nativeLifecycleFor(runtimeStats.lifecycle),
        .renderThreadRunning = runtimeStats.renderThreadRunning ? 1U : 0U,
        .renderThreadJoined = runtimeStats.renderThreadJoined ? 1U : 0U,
        .callerIsRenderThread = runtimeStats.renderThreadId == std::this_thread::get_id() ? 1U : 0U,
    };
    return EditorViewportNativeStatus_Success;
}

void EDITOR_NATIVE_CALL editor_viewport_shutdown() {
    asharia::editor::EditorSharedViewportRuntime::instance().shutdown();
}

} // extern "C"
