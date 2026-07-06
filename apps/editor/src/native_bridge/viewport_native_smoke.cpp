#include "native_bridge/viewport_native_smoke.hpp"

#include <cstdint>

#include "asharia/core/log.hpp"

#include "native_bridge/viewport_native_api.hpp"

namespace asharia::editor {
    namespace {

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

        void releaseIfNeeded(EditorViewportNativeCompatibilityResult result) {
            if (result.messageUtf8 != nullptr) {
                editor_viewport_release_compatibility_result(result);
            }
        }

        void releaseIfNeeded(EditorViewportNativePresentPacket packet) {
            if (packet.messageUtf8 != nullptr) {
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
        const std::uint32_t packetStatus =
            editor_viewport_acquire_present_packet(&supportedRequest, &packet);
        const bool packetUnavailable = packetStatus == EditorViewportNativeStatus_Unavailable &&
                                       packet.status == EditorViewportNativeStatus_Unavailable;
        releaseIfNeeded(packet);
        if (!packetUnavailable) {
            logError("Viewport native bridge smoke expected present packet acquisition to wait for B2.");
            return false;
        }

        return true;
    }

} // namespace asharia::editor
