#include "native_bridge/viewport_native_smoke.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string_view>

#include "asharia/core/log.hpp"

#include "native_bridge/viewport_native_api.hpp"

namespace asharia::editor {
    namespace {

        void logPresentPacketMessage(const EditorViewportNativePresentPacket& packet) {
            if (packet.messageUtf8 == nullptr || packet.messageByteLength == 0U) {
                return;
            }

            const auto message = std::string_view{
                static_cast<const char*>(packet.messageUtf8), packet.messageByteLength};
            logError(message);
        }

        class SharedViewportRuntimeShutdown final {
        public:
            SharedViewportRuntimeShutdown() = default;
            SharedViewportRuntimeShutdown(const SharedViewportRuntimeShutdown&) = delete;
            SharedViewportRuntimeShutdown& operator=(const SharedViewportRuntimeShutdown&) = delete;
            SharedViewportRuntimeShutdown(SharedViewportRuntimeShutdown&&) = delete;
            SharedViewportRuntimeShutdown& operator=(SharedViewportRuntimeShutdown&&) = delete;

            ~SharedViewportRuntimeShutdown() {
                editor_viewport_shutdown();
            }
        };

        [[nodiscard]] EditorViewportNativeCompatibilityRequest makeRequest(
            std::uint32_t imageHandleType = EditorViewportNativeHandleType_VulkanOpaqueNt,
            std::uint32_t semaphoreHandleType = EditorViewportNativeHandleType_VulkanOpaqueNt) {
            return EditorViewportNativeCompatibilityRequest{
                .header =
                    EditorViewportNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize = static_cast<std::uint32_t>(
                            sizeof(EditorViewportNativeCompatibilityRequest)),
                    },
                .imageHandleType = imageHandleType,
                .semaphoreHandleType = semaphoreHandleType,
                .deviceLuidLowPart = 0U,
                .deviceLuidHighPart = 0,
                .hasDeviceLuid = 0U,
                .deviceUuidLow = 0U,
                .deviceUuidHigh = 0U,
                .hasDeviceUuid = 0U,
            };
        }

        [[nodiscard]] EditorViewportNativeCompatibilityRequest makeUndersizedRequest() {
            EditorViewportNativeCompatibilityRequest request = makeRequest();
            request.header.structSize =
                static_cast<std::uint32_t>(sizeof(EditorViewportNativeAbiHeader));
            return request;
        }

        [[nodiscard]] EditorViewportNativeCompatibilityRequest makeMismatchedUuidRequest() {
            EditorViewportNativeCompatibilityRequest request = makeRequest();
            request.hasDeviceUuid = 1U;
            request.deviceUuidLow = 0x1111111111111111UL;
            request.deviceUuidHigh = 0x2222222222222222UL;
            return request;
        }

        [[nodiscard]] EditorViewportNativePresentRequest makePresentRequest(VkExtent2D extent) {
            return EditorViewportNativePresentRequest{
                .header =
                    EditorViewportNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize =
                            static_cast<std::uint32_t>(sizeof(EditorViewportNativePresentRequest)),
                    },
                .compatibility = makeRequest(),
                .widthPixels = extent.width,
                .heightPixels = extent.height,
            };
        }

        void releaseIfNeeded(EditorViewportNativeCompatibilityResult result) {
            if (result.messageUtf8 != nullptr) {
                editor_viewport_release_compatibility_result(result);
            }
        }

        void releaseIfNeeded(EditorViewportNativePresentPacket packet) {
            if (packet.nativePacket != nullptr || packet.messageUtf8 != nullptr) {
                editor_viewport_release_present_packet(packet);
            }
        }

        [[nodiscard]] bool expectCompatibilityStatus(
            const EditorViewportNativeCompatibilityRequest* request,
            std::uint32_t expectedStatus) {
            EditorViewportNativeCompatibilityResult result{};
            const std::uint32_t status =
                editor_viewport_query_composition_compatibility(request, &result);
            const bool matches = status == expectedStatus && result.status == expectedStatus;
            releaseIfNeeded(result);
            return matches;
        }

    } // namespace

    bool runViewportNativeBridgeSmoke() {
        const SharedViewportRuntimeShutdown shutdownOnExit;

        if (!expectCompatibilityStatus(nullptr, EditorViewportNativeStatus_InvalidArgument)) {
            logError("Viewport native bridge smoke did not reject a null compatibility request.");
            return false;
        }

        EditorViewportNativeCompatibilityRequest undersizedRequest = makeUndersizedRequest();
        if (!expectCompatibilityStatus(&undersizedRequest,
                                       EditorViewportNativeStatus_UnsupportedAbi)) {
            logError("Viewport native bridge smoke did not reject an undersized ABI request.");
            return false;
        }

        EditorViewportNativeCompatibilityRequest unknownHandleRequest =
            makeRequest(EditorViewportNativeHandleType_Unknown,
                        EditorViewportNativeHandleType_VulkanOpaqueNt);
        if (!expectCompatibilityStatus(&unknownHandleRequest,
                                       EditorViewportNativeStatus_UnsupportedHandleType)) {
            logError("Viewport native bridge smoke did not reject an unknown image handle type.");
            return false;
        }

        EditorViewportNativeCompatibilityRequest supportedRequest = makeRequest();
        EditorViewportNativeCompatibilityResult supportedResult{};
        const std::uint32_t supportedStatus =
            editor_viewport_query_composition_compatibility(&supportedRequest, &supportedResult);
        const bool supported =
            supportedStatus == EditorViewportNativeStatus_Success &&
            supportedResult.status == EditorViewportNativeStatus_Success &&
            supportedResult.producedImageHandleType ==
                EditorViewportNativeHandleType_VulkanOpaqueNt &&
            supportedResult.producedSemaphoreHandleType ==
                EditorViewportNativeHandleType_VulkanOpaqueNt;
        releaseIfNeeded(supportedResult);
        if (!supported) {
            logError("Viewport native bridge smoke did not accept a Vulkan opaque NT request.");
            return false;
        }

        EditorViewportNativeCompatibilityRequest mismatchedRequest = makeMismatchedUuidRequest();
        if (!expectCompatibilityStatus(&mismatchedRequest,
                                       EditorViewportNativeStatus_DeviceMismatch)) {
            logError("Viewport native bridge smoke did not detect a mismatched device UUID.");
            return false;
        }

        EditorViewportNativePresentPacket packet{};
        EditorViewportNativePresentRequest firstPresentRequest =
            makePresentRequest(VkExtent2D{.width = 320U, .height = 180U});
        const std::uint32_t packetStatus =
            editor_viewport_acquire_present_packet(&firstPresentRequest, &packet);
        const bool packetAvailable =
            packetStatus == EditorViewportNativeStatus_Success &&
            packet.status == EditorViewportNativeStatus_Success &&
            packet.nativePacket != nullptr && packet.imageHandle != nullptr &&
            packet.waitSemaphoreHandle != nullptr && packet.signalSemaphoreHandle != nullptr &&
            packet.widthPixels == 320U && packet.heightPixels == 180U &&
            packet.format == EditorViewportNativeImageFormat_Bgra8Unorm &&
            packet.memorySizeBytes >= 320ULL * 180ULL * 4ULL && packet.frameIndex == 1U;
        if (!packetAvailable) {
            logPresentPacketMessage(packet);
            releaseIfNeeded(packet);
            logError("Viewport native bridge smoke did not produce the first shared present packet.");
            return false;
        }
        releaseIfNeeded(packet);

        EditorViewportNativePresentPacket resizedPacket{};
        EditorViewportNativePresentRequest resizedPresentRequest =
            makePresentRequest(VkExtent2D{.width = 640U, .height = 360U});
        const std::uint32_t resizedPacketStatus =
            editor_viewport_acquire_present_packet(&resizedPresentRequest, &resizedPacket);
        const bool resizedPacketAvailable =
            resizedPacketStatus == EditorViewportNativeStatus_Success &&
            resizedPacket.status == EditorViewportNativeStatus_Success &&
            resizedPacket.nativePacket != nullptr && resizedPacket.imageHandle != nullptr &&
            resizedPacket.waitSemaphoreHandle != nullptr &&
            resizedPacket.signalSemaphoreHandle != nullptr && resizedPacket.widthPixels == 640U &&
            resizedPacket.heightPixels == 360U && resizedPacket.frameIndex == 2U;
        if (!resizedPacketAvailable) {
            logPresentPacketMessage(resizedPacket);
            releaseIfNeeded(resizedPacket);
            logError("Viewport native bridge smoke did not produce a resized shared present packet.");
            return false;
        }
        releaseIfNeeded(resizedPacket);

        return true;
    }

} // namespace asharia::editor
