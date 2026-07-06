#include "native_bridge/viewport_native_api.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

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

    [[nodiscard]] bool hasSupportedRequestHeader(
        const EditorViewportNativeCompatibilityRequest& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >=
                   sizeof(EditorViewportNativeCompatibilityRequest);
    }

    [[nodiscard]] bool hasSupportedPresentRequestHeader(
        const EditorViewportNativePresentRequest& request) {
        return request.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
               request.header.structSize >= sizeof(EditorViewportNativePresentRequest) &&
               hasSupportedRequestHeader(request.compatibility);
    }

    [[nodiscard]] bool hasSupportedHandleTypes(
        const EditorViewportNativeCompatibilityRequest& request) {
        return request.imageHandleType == EditorViewportNativeHandleType_VulkanOpaqueNt &&
               request.semaphoreHandleType == EditorViewportNativeHandleType_VulkanOpaqueNt;
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

    [[nodiscard]] std::uint64_t readUuidLow(
        const std::array<std::uint8_t, VK_UUID_SIZE>& bytes) {
        return static_cast<std::uint64_t>(bytes[0]) |
               (static_cast<std::uint64_t>(bytes[1]) << 8U) |
               (static_cast<std::uint64_t>(bytes[2]) << 16U) |
               (static_cast<std::uint64_t>(bytes[3]) << 24U) |
               (static_cast<std::uint64_t>(bytes[4]) << 32U) |
               (static_cast<std::uint64_t>(bytes[5]) << 40U) |
               (static_cast<std::uint64_t>(bytes[6]) << 48U) |
               (static_cast<std::uint64_t>(bytes[7]) << 56U);
    }

    [[nodiscard]] std::uint64_t readUuidHigh(
        const std::array<std::uint8_t, VK_UUID_SIZE>& bytes) {
        return static_cast<std::uint64_t>(bytes[8]) |
               (static_cast<std::uint64_t>(bytes[9]) << 8U) |
               (static_cast<std::uint64_t>(bytes[10]) << 16U) |
               (static_cast<std::uint64_t>(bytes[11]) << 24U) |
               (static_cast<std::uint64_t>(bytes[12]) << 32U) |
               (static_cast<std::uint64_t>(bytes[13]) << 40U) |
               (static_cast<std::uint64_t>(bytes[14]) << 48U) |
               (static_cast<std::uint64_t>(bytes[15]) << 56U);
    }

    [[nodiscard]] std::uint32_t readLuidLow(
        const std::array<std::uint8_t, VK_LUID_SIZE>& bytes) {
        return static_cast<std::uint32_t>(bytes[0]) |
               (static_cast<std::uint32_t>(bytes[1]) << 8U) |
               (static_cast<std::uint32_t>(bytes[2]) << 16U) |
               (static_cast<std::uint32_t>(bytes[3]) << 24U);
    }

    [[nodiscard]] std::int32_t readLuidHigh(
        const std::array<std::uint8_t, VK_LUID_SIZE>& bytes) {
        const std::uint32_t value =
            static_cast<std::uint32_t>(bytes[4]) |
            (static_cast<std::uint32_t>(bytes[5]) << 8U) |
            (static_cast<std::uint32_t>(bytes[6]) << 16U) |
            (static_cast<std::uint32_t>(bytes[7]) << 24U);
        return std::bit_cast<std::int32_t>(value);
    }

    [[nodiscard]] bool matchesRequestedDevice(
        const EditorViewportNativeCompatibilityRequest& request,
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
        EditorViewportNativeCompatibilityResult* result,
        std::uint32_t status,
        const asharia::VulkanDeviceInfo* deviceInfo,
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
            .nativeDeviceVendorId = deviceInfo != nullptr ? deviceInfo->vendorId : 0U,
            .nativeDeviceId = deviceInfo != nullptr ? deviceInfo->deviceId : 0U,
            .nativeDeviceUuidLow =
                deviceInfo != nullptr ? readUuidLow(deviceInfo->identity.deviceUuid) : 0U,
            .nativeDeviceUuidHigh =
                deviceInfo != nullptr ? readUuidHigh(deviceInfo->identity.deviceUuid) : 0U,
            .messageUtf8 = messageData,
            .messageByteLength = messageByteLength,
        };
        return status;
    }

    [[nodiscard]] std::uint32_t writePresentPacketFailure(
        EditorViewportNativePresentPacket* packet, std::uint32_t status,
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

    [[nodiscard]] std::uint32_t writePresentPacketSuccess(
        EditorViewportNativePresentPacket* packet,
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
        return writeCompatibilityResult(result, EditorViewportNativeStatus_UnsupportedHandleType,
                                        nullptr,
                                        "Vulkan opaque NT image and semaphore handles are required.");
    }

    auto context = asharia::editor::EditorSharedViewportRuntime::instance().ensureContext();
    if (!context) {
        return writeCompatibilityResult(result, EditorViewportNativeStatus_Unavailable, nullptr,
                                        context.error().message);
    }

    const asharia::VulkanDeviceInfo& deviceInfo = (*context)->deviceInfo();
    if (!matchesRequestedDevice(*request, deviceInfo.identity)) {
        return writeCompatibilityResult(result, EditorViewportNativeStatus_DeviceMismatch,
                                        &deviceInfo,
                                        "Avalonia compositor device does not match the Vulkan viewport device.");
    }

    return writeCompatibilityResult(result, EditorViewportNativeStatus_Success, &deviceInfo,
                                    "Vulkan viewport device is compatible with Avalonia composition.");
}

void EDITOR_NATIVE_CALL editor_viewport_release_compatibility_result(
    EditorViewportNativeCompatibilityResult result) {
    const std::unique_ptr<std::byte[]> message{
        static_cast<std::byte*>(result.messageUtf8)};
}

std::uint32_t EDITOR_NATIVE_CALL editor_viewport_acquire_present_packet(
    const EditorViewportNativePresentRequest* request,
    EditorViewportNativePresentPacket* packet) {
    if (request == nullptr || packet == nullptr) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }

    if (!hasSupportedPresentRequestHeader(*request)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_UnsupportedAbi);
        return EditorViewportNativeStatus_UnsupportedAbi;
    }

    if (!hasSupportedHandleTypes(request->compatibility)) {
        clearPresentPacket(packet, EditorViewportNativeStatus_UnsupportedHandleType);
        return EditorViewportNativeStatus_UnsupportedHandleType;
    }

    if (request->widthPixels == 0U || request->heightPixels == 0U) {
        clearPresentPacket(packet, EditorViewportNativeStatus_InvalidArgument);
        return EditorViewportNativeStatus_InvalidArgument;
    }

    auto context = asharia::editor::EditorSharedViewportRuntime::instance().ensureContext();
    if (!context) {
        return writePresentPacketFailure(packet, EditorViewportNativeStatus_Unavailable,
                                         context.error().message);
    }

    const asharia::VulkanDeviceInfo& deviceInfo = (*context)->deviceInfo();
    if (!matchesRequestedDevice(request->compatibility, deviceInfo.identity)) {
        return writePresentPacketFailure(
            packet, EditorViewportNativeStatus_DeviceMismatch,
            "Avalonia compositor device does not match the Vulkan viewport device.");
    }

    auto present = asharia::editor::EditorSharedViewportRuntime::instance().renderSceneViewFrame(
        asharia::editor::EditorSharedViewportPresentDesc{
            .panelId = "scene-view/native",
            .kind = asharia::editor::EditorViewportKind::Scene,
            .extent =
                asharia::editor::EditorExtent2D{
                    .width = request->widthPixels,
                    .height = request->heightPixels,
                },
        });
    if (!present) {
        return writePresentPacketFailure(packet, EditorViewportNativeStatus_RenderFailed,
                                         present.error().message);
    }

    return writePresentPacketSuccess(packet, *present);
}

void EDITOR_NATIVE_CALL editor_viewport_release_present_packet(
    EditorViewportNativePresentPacket packet) {
    asharia::editor::EditorSharedViewportRuntime::instance().releasePresentPacket(
        packet.nativePacket);
    const std::unique_ptr<std::byte[]> message{static_cast<std::byte*>(packet.messageUtf8)};
}

void EDITOR_NATIVE_CALL editor_viewport_shutdown() {
    asharia::editor::EditorSharedViewportRuntime::instance().shutdown();
}

} // extern "C"
