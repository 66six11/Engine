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
#include <span>
#include <string_view>
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

    [[nodiscard]] EditorViewportNativeAbiHeader presentPacketHeader() {
        return EditorViewportNativeAbiHeader{
            .abiVersion = EDITOR_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(sizeof(EditorViewportNativePresentPacket)),
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

    [[nodiscard]] bool
    hasSupportedRequestHeader(const EditorViewportNativeCompatibilityRequest& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >= sizeof(EditorViewportNativeCompatibilityRequest);
    }

    [[nodiscard]] bool
    hasSupportedPresentRequestHeader(const EditorViewportNativePresentRequest& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >= sizeof(EditorViewportNativePresentRequest) &&
               hasSupportedRequestHeader(request.compatibility);
    }

    [[nodiscard]] bool
    hasSupportedPresentRequestV2Header(const EditorViewportNativePresentRequestV2& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >= sizeof(EditorViewportNativePresentRequestV2) &&
               hasSupportedRequestHeader(request.compatibility);
    }

    [[nodiscard]] bool
    hasSupportedPresentRequestV4Header(const EditorViewportNativePresentRequestV4& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >= sizeof(EditorViewportNativePresentRequestV4) &&
               hasSupportedRequestHeader(request.compatibility);
    }

    [[nodiscard]] bool hasSupportedPresentSlotRenderRequestHeader(
        const EditorViewportNativePresentSlotRenderRequest& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >= sizeof(EditorViewportNativePresentSlotRenderRequest);
    }

    [[nodiscard]] bool
    hasSupportedPresentPacketHeader(const EditorViewportNativePresentPacket& packet) {
        return packet.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               packet.header.structSize >= sizeof(EditorViewportNativePresentPacket);
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
            !std::isfinite(camera.verticalFovRadians) || !std::isfinite(camera.nearPlane) ||
            !std::isfinite(camera.farPlane) || camera.verticalFovRadians <= 0.0F ||
            camera.verticalFovRadians >= std::numbers::pi_v<float> || camera.nearPlane <= 0.0F ||
            camera.farPlane <= camera.nearPlane) {
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

    [[nodiscard]] bool validPresentRequestV4(const EditorViewportNativePresentRequestV4& request) {
        constexpr std::uint32_t kMaximumDebugProxyCount = 256U;
        if (!hasValue(request.sessionId) || !hasValue(request.targetId) ||
            request.targetRevision == 0U || request.requestSequence == 0U ||
            request.widthPixels == 0U || request.heightPixels == 0U || request.reserved != 0U ||
            request.kind > EditorViewportNativeRenderKind_Preview ||
            request.targetKind != EditorViewportNativeTargetKind_DocumentScene ||
            request.debugProxyCount > kMaximumDebugProxyCount ||
            (request.debugProxyCount != 0U && request.debugProxies == nullptr) ||
            !validCamera(request.camera)) {
            return false;
        }
        if (request.debugProxyCount == 0U) {
            return true;
        }
        const std::span<const EditorViewportNativeDebugProxy> debugProxies{request.debugProxies,
                                                                           request.debugProxyCount};
        return std::ranges::all_of(debugProxies, [](const EditorViewportNativeDebugProxy& proxy) {
            return validDebugProxy(proxy);
        });
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
        result.verticalFovRadians = camera.verticalFovRadians;
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

    void clearPresentPacket(EditorViewportNativePresentPacket* packet, std::uint32_t status) {
        if (packet == nullptr) {
            return;
        }

        *packet = EditorViewportNativePresentPacket{
            .header = presentPacketHeader(),
            .status = status,
            .nativePacket = nullptr,
            .imageHandle = nullptr,
            .waitSemaphoreHandle = nullptr,
            .signalSemaphoreHandle = nullptr,
            .widthPixels = 0U,
            .heightPixels = 0U,
            .format = EditorViewportNativeImageFormat_Unknown,
            .memorySizeBytes = 0U,
            .frameIndex = 0U,
            .messageUtf8 = nullptr,
            .messageByteLength = 0U,
        };
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

    [[nodiscard]] std::uint32_t writePresentPacketFailure(EditorViewportNativePresentPacket* packet,
                                                          std::uint32_t status,
                                                          std::string_view message) {
        void* messageData{};
        std::uint64_t messageByteLength{};
        if (!allocateMessage(message, messageData, messageByteLength)) {
            clearPresentPacket(packet, EditorViewportNativeStatus_InternalError);
            return EditorViewportNativeStatus_InternalError;
        }

        *packet = EditorViewportNativePresentPacket{
            .header = presentPacketHeader(),
            .status = status,
            .nativePacket = nullptr,
            .imageHandle = nullptr,
            .waitSemaphoreHandle = nullptr,
            .signalSemaphoreHandle = nullptr,
            .widthPixels = 0U,
            .heightPixels = 0U,
            .format = EditorViewportNativeImageFormat_Unknown,
            .memorySizeBytes = 0U,
            .frameIndex = 0U,
            .messageUtf8 = messageData,
            .messageByteLength = messageByteLength,
        };
        return status;
    }

    [[nodiscard]] std::uint32_t
    writePresentPacketSuccess(EditorViewportNativePresentPacket* packet,
                              const asharia::editor::EditorSharedViewportPresentPacket& present) {
        std::uint32_t format = EditorViewportNativeImageFormat_Unknown;
        if (present.format == VK_FORMAT_R8G8B8A8_UNORM) {
            format = EditorViewportNativeImageFormat_Rgba8Unorm;
        } else if (present.format == VK_FORMAT_B8G8R8A8_UNORM) {
            format = EditorViewportNativeImageFormat_Bgra8Unorm;
        }
        if (format == EditorViewportNativeImageFormat_Unknown) {
            asharia::editor::EditorSharedViewportRuntime::instance().releasePresentPacket(
                present.nativePacket);
            return writePresentPacketFailure(
                packet, EditorViewportNativeStatus_RenderFailed,
                "Shared viewport renderer produced an unsupported image format.");
        }

        *packet = EditorViewportNativePresentPacket{
            .header = presentPacketHeader(),
            .status = EditorViewportNativeStatus_Success,
            .nativePacket = present.nativePacket,
            .imageHandle = present.imageHandle,
            .waitSemaphoreHandle = present.waitSemaphoreHandle,
            .signalSemaphoreHandle = present.signalSemaphoreHandle,
            .widthPixels = present.extent.width,
            .heightPixels = present.extent.height,
            .format = format,
            .memorySizeBytes = present.memorySizeBytes,
            .frameIndex = present.frameIndex,
            .messageUtf8 = nullptr,
            .messageByteLength = 0U,
        };
        return EditorViewportNativeStatus_Success;
    }

    [[nodiscard]] std::uint32_t
    acquirePresentPacket(const EditorViewportNativeCompatibilityRequest& compatibility,
                         std::uint32_t widthPixels, std::uint32_t heightPixels, bool hasScene,
                         std::uint64_t sceneRevision, bool reusableSlot,
                         EditorViewportNativePresentPacket* packet) {
        if (!hasSupportedHandleTypes(compatibility)) {
            clearPresentPacket(packet, EditorViewportNativeStatus_UnsupportedHandleType);
            return EditorViewportNativeStatus_UnsupportedHandleType;
        }

        if (widthPixels == 0U || heightPixels == 0U) {
            clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
            return EditorViewportNativeStatus_InvalidArgument;
        }

        const auto deviceSnapshot =
            asharia::editor::EditorSharedViewportRuntime::instance().ensureDeviceSnapshot();
        if (!deviceSnapshot) {
            return writePresentPacketFailure(packet, EditorViewportNativeStatus_Unavailable,
                                             deviceSnapshot.error().message);
        }

        if (!matchesRequestedDevice(compatibility, deviceSnapshot->identity)) {
            return writePresentPacketFailure(
                packet, EditorViewportNativeStatus_DeviceMismatch,
                "Avalonia compositor device does not match the Vulkan viewport device.");
        }

        const asharia::editor::EditorSharedViewportPresentDesc desc{
            .panelId = "scene-view/native",
            .kind = asharia::editor::EditorViewportKind::Scene,
            .extent =
                asharia::editor::EditorExtent2D{
                    .width = widthPixels,
                    .height = heightPixels,
                },
            .hasScene = hasScene,
            .sceneRevision = sceneRevision,
            .sessionId = {},
            .targetId = {},
            .requestSequence = 0U,
            .hasCamera = false,
            .camera = {},
            .debugProxies = {},
        };
        auto present =
            reusableSlot
                ? asharia::editor::EditorSharedViewportRuntime::instance().createPresentSlot(desc)
                : asharia::editor::EditorSharedViewportRuntime::instance().renderSceneViewFrame(
                      desc);
        if (!present) {
            const asharia::editor::EditorSharedViewportRenderFrameError& error = present.error();
            const std::uint32_t status =
                error.kind ==
                        asharia::editor::EditorSharedViewportRenderFrameErrorKind::Backpressure
                    ? EditorViewportNativeStatus_Unavailable
                    : EditorViewportNativeStatus_RenderFailed;
            return writePresentPacketFailure(packet, status, error.error.message);
        }

        return writePresentPacketSuccess(packet, *present);
    }

    [[nodiscard]] std::uint32_t
    createPresentSlotV4(const EditorViewportNativePresentRequestV4& request,
                        EditorViewportNativePresentPacket* packet) {
        if (!hasSupportedHandleTypes(request.compatibility)) {
            clearPresentPacket(packet, EditorViewportNativeStatus_UnsupportedHandleType);
            return EditorViewportNativeStatus_UnsupportedHandleType;
        }

        const auto deviceSnapshot =
            asharia::editor::EditorSharedViewportRuntime::instance().ensureDeviceSnapshot();
        if (!deviceSnapshot) {
            return writePresentPacketFailure(packet, EditorViewportNativeStatus_Unavailable,
                                             deviceSnapshot.error().message);
        }
        if (!matchesRequestedDevice(request.compatibility, deviceSnapshot->identity)) {
            return writePresentPacketFailure(
                packet, EditorViewportNativeStatus_DeviceMismatch,
                "Avalonia compositor device does not match the Vulkan viewport device.");
        }

        std::vector<asharia::editor::EditorSharedViewportDebugProxy> debugProxies;
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
        } catch (const std::bad_alloc&) {
            return writePresentPacketFailure(packet, EditorViewportNativeStatus_InternalError,
                                             "Viewport debug proxy allocation failed.");
        }

        const asharia::editor::EditorSharedViewportPresentDesc desc{
            .panelId = "viewport-session/native",
            .kind = viewportKind(request.kind),
            .extent =
                asharia::editor::EditorExtent2D{
                    .width = request.widthPixels,
                    .height = request.heightPixels,
                },
            .hasScene = true,
            .sceneRevision = request.targetRevision,
            .sessionId = {request.sessionId.low, request.sessionId.high},
            .targetId = {request.targetId.low, request.targetId.high},
            .requestSequence = request.requestSequence,
            .hasCamera = true,
            .camera = viewportCamera(request.camera),
            .debugProxies = debugProxies,
        };
        auto present =
            asharia::editor::EditorSharedViewportRuntime::instance().createPresentSlot(desc);
        if (!present) {
            const asharia::editor::EditorSharedViewportRenderFrameError& error = present.error();
            const std::uint32_t status =
                error.kind ==
                        asharia::editor::EditorSharedViewportRenderFrameErrorKind::Backpressure
                    ? EditorViewportNativeStatus_Unavailable
                    : EditorViewportNativeStatus_RenderFailed;
            return writePresentPacketFailure(packet, status, error.error.message);
        }
        return writePresentPacketSuccess(packet, *present);
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

std::uint32_t EDITOR_NATIVE_CALL editor_viewport_acquire_present_packet(
    const EditorViewportNativePresentRequest* request, EditorViewportNativePresentPacket* packet) {
    if (request == nullptr || packet == nullptr) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }

    if (!hasSupportedPresentRequestHeader(*request)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_UnsupportedAbi);
        return EditorViewportNativeStatus_UnsupportedAbi;
    }

    return acquirePresentPacket(request->compatibility, request->widthPixels, request->heightPixels,
                                false, 0U, false, packet);
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_acquire_present_packet_v2(const EditorViewportNativePresentRequestV2* request,
                                          EditorViewportNativePresentPacket* packet) {
    if (request == nullptr || packet == nullptr) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }

    if (!hasSupportedPresentRequestV2Header(*request)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_UnsupportedAbi);
        return EditorViewportNativeStatus_UnsupportedAbi;
    }

    if (request->hasScene > 1U || request->reserved != 0U ||
        (request->hasScene == 0U && request->sceneRevision != 0U)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }

    return acquirePresentPacket(request->compatibility, request->widthPixels, request->heightPixels,
                                request->hasScene != 0U, request->sceneRevision, false, packet);
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_create_present_slot_v3(const EditorViewportNativePresentRequestV2* request,
                                       EditorViewportNativePresentPacket* packet) {
    if (request == nullptr || packet == nullptr) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }
    if (!hasSupportedPresentRequestV2Header(*request)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_UnsupportedAbi);
        return EditorViewportNativeStatus_UnsupportedAbi;
    }
    if (request->hasScene > 1U || request->reserved != 0U ||
        (request->hasScene == 0U && request->sceneRevision != 0U)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }

    return acquirePresentPacket(request->compatibility, request->widthPixels, request->heightPixels,
                                request->hasScene != 0U, request->sceneRevision, true, packet);
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_render_present_slot_v3(const EditorViewportNativePresentSlotRenderRequest* request,
                                       EditorViewportNativePresentPacket* packet) {
    if (request == nullptr || packet == nullptr) {
        return EditorViewportNativeStatus_InvalidArgument;
    }
    if (!hasSupportedPresentSlotRenderRequestHeader(*request) ||
        !hasSupportedPresentPacketHeader(*packet)) {
        return EditorViewportNativeStatus_UnsupportedAbi;
    }
    if (request->nativeSlot == nullptr || request->nativeSlot != packet->nativePacket ||
        request->widthPixels == 0U || request->heightPixels == 0U ||
        request->widthPixels != packet->widthPixels ||
        request->heightPixels != packet->heightPixels || request->hasScene > 1U ||
        request->reserved != 0U || (request->hasScene == 0U && request->sceneRevision != 0U)) {
        return EditorViewportNativeStatus_InvalidArgument;
    }

    auto present = asharia::editor::EditorSharedViewportRuntime::instance().renderPresentSlot(
        request->nativeSlot, asharia::editor::EditorSharedViewportPresentDesc{
                                 .panelId = "scene-view/native",
                                 .kind = asharia::editor::EditorViewportKind::Scene,
                                 .extent =
                                     asharia::editor::EditorExtent2D{
                                         .width = request->widthPixels,
                                         .height = request->heightPixels,
                                     },
                                 .hasScene = request->hasScene != 0U,
                                 .sceneRevision = request->sceneRevision,
                                 .sessionId = {},
                                 .targetId = {},
                                 .requestSequence = 0U,
                                 .hasCamera = false,
                                 .camera = {},
                                 .debugProxies = {},
                             });
    if (!present) {
        const asharia::editor::EditorSharedViewportRenderFrameError& error = present.error();
        return error.kind == asharia::editor::EditorSharedViewportRenderFrameErrorKind::Backpressure
                   ? EditorViewportNativeStatus_Unavailable
                   : EditorViewportNativeStatus_RenderFailed;
    }

    return writePresentPacketSuccess(packet, *present);
}

std::uint32_t EDITOR_NATIVE_CALL
editor_viewport_create_present_slot_v4(const EditorViewportNativePresentRequestV4* request,
                                       EditorViewportNativePresentPacket* packet) {
    if (request == nullptr || packet == nullptr) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }
    if (!hasSupportedPresentRequestV4Header(*request)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_UnsupportedAbi);
        return EditorViewportNativeStatus_UnsupportedAbi;
    }
    if (!validPresentRequestV4(*request)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }
    try {
        return createPresentSlotV4(*request, packet);
    } catch (const std::bad_alloc&) {
        return writePresentPacketFailure(packet, EditorViewportNativeStatus_InternalError,
                                         "Viewport request allocation failed.");
    } catch (const std::exception& exception) {
        return writePresentPacketFailure(packet, EditorViewportNativeStatus_InternalError,
                                         exception.what());
    } catch (...) {
        return writePresentPacketFailure(packet, EditorViewportNativeStatus_InternalError,
                                         "Viewport request failed with an unknown exception.");
    }
}

void EDITOR_NATIVE_CALL
editor_viewport_release_present_packet(EditorViewportNativePresentPacket packet) {
    asharia::editor::EditorSharedViewportRuntime::instance().releasePresentPacket(
        packet.nativePacket);
    const std::unique_ptr<std::byte[]> message{static_cast<std::byte*>(packet.messageUtf8)};
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

void EDITOR_NATIVE_CALL editor_viewport_shutdown() {
    asharia::editor::EditorSharedViewportRuntime::instance().shutdown();
}

} // extern "C"
