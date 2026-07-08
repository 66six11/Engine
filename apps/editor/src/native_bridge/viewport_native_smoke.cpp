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

        [[nodiscard]] bool queryRuntimeStatsV2(EditorViewportNativeRuntimeStatsV2& stats) {
            const std::uint32_t status = editor_viewport_query_runtime_stats_v2(&stats);
            return status == EditorViewportNativeStatus_Success &&
                   stats.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
                   stats.header.structSize == sizeof(EditorViewportNativeRuntimeStatsV2);
        }

        [[nodiscard]] bool queryRuntimeStatsV3(EditorViewportNativeRuntimeStatsV3& stats) {
            const std::uint32_t status = editor_viewport_query_runtime_stats_v3(&stats);
            return status == EditorViewportNativeStatus_Success &&
                   stats.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
                   stats.header.structSize == sizeof(EditorViewportNativeRuntimeStatsV3);
        }

        [[nodiscard]] bool queryRuntimeStatsV4(EditorViewportNativeRuntimeStatsV4& stats) {
            const std::uint32_t status = editor_viewport_query_runtime_stats_v4(&stats);
            return status == EditorViewportNativeStatus_Success &&
                   stats.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
                   stats.header.structSize == sizeof(EditorViewportNativeRuntimeStatsV4);
        }

        [[nodiscard]] bool queryRuntimeStatsV5(EditorViewportNativeRuntimeStatsV5& stats) {
            const std::uint32_t status = editor_viewport_query_runtime_stats_v5(&stats);
            return status == EditorViewportNativeStatus_Success &&
                   stats.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
                   stats.header.structSize == sizeof(EditorViewportNativeRuntimeStatsV5);
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
        EditorViewportNativeRuntimeStats statsAfterFirstPacket{};
        const std::uint32_t statsStatus =
            editor_viewport_query_runtime_stats(&statsAfterFirstPacket);
        if (statsStatus != EditorViewportNativeStatus_Success ||
            statsAfterFirstPacket.framesRendered != 1U ||
            statsAfterFirstPacket.producersCreated != 1U ||
            statsAfterFirstPacket.packetsCreated != 1U ||
            statsAfterFirstPacket.outstandingPackets != 1U ||
            statsAfterFirstPacket.hasRenderProducer == 0U) {
            releaseIfNeeded(packet);
            logError("Viewport native bridge smoke did not expose first render producer stats.");
            return false;
        }
        EditorViewportNativeRuntimeStatsV2 statsV2AfterFirstPacket{};
        const std::uint32_t statsV2Status =
            editor_viewport_query_runtime_stats_v2(&statsV2AfterFirstPacket);
        if (statsV2Status != EditorViewportNativeStatus_Success ||
            statsV2AfterFirstPacket.header.structSize !=
                sizeof(EditorViewportNativeRuntimeStatsV2) ||
            statsV2AfterFirstPacket.framesRendered != 1U ||
            statsV2AfterFirstPacket.producersCreated != 1U ||
            statsV2AfterFirstPacket.packetsCreated != 1U ||
            statsV2AfterFirstPacket.outstandingPackets != 1U ||
            statsV2AfterFirstPacket.hasRenderProducer == 0U) {
            releaseIfNeeded(packet);
            logError("Viewport native bridge smoke did not expose runtime stats v2.");
            return false;
        }
        EditorViewportNativeRuntimeStatsV3 statsV3AfterFirstPacket{};
        if (!queryRuntimeStatsV3(statsV3AfterFirstPacket) ||
            statsV3AfterFirstPacket.framesRendered != 1U ||
            statsV3AfterFirstPacket.producersCreated != 1U ||
            statsV3AfterFirstPacket.packetsCreated != 1U ||
            statsV3AfterFirstPacket.outstandingPackets != 1U ||
            statsV3AfterFirstPacket.hasRenderProducer == 0U ||
            statsV3AfterFirstPacket.frameEpochsSubmitted != 1U ||
            statsV3AfterFirstPacket.frameEpochsCompleted != 0U ||
            statsV3AfterFirstPacket.frameEpochsPending != 1U) {
            releaseIfNeeded(packet);
            logError("Viewport native bridge smoke did not expose runtime stats v3 before release.");
            return false;
        }
        EditorViewportNativeRuntimeStatsV4 statsV4AfterFirstPacket{};
        if (!queryRuntimeStatsV4(statsV4AfterFirstPacket) ||
            statsV4AfterFirstPacket.framesRendered != 1U ||
            statsV4AfterFirstPacket.producersCreated != 1U ||
            statsV4AfterFirstPacket.packetsCreated != 1U ||
            statsV4AfterFirstPacket.outstandingPackets != 1U ||
            statsV4AfterFirstPacket.hasRenderProducer == 0U ||
            statsV4AfterFirstPacket.frameEpochsSubmitted != 1U ||
            statsV4AfterFirstPacket.frameEpochsCompleted != 0U ||
            statsV4AfterFirstPacket.frameEpochsPending != 1U ||
            statsV4AfterFirstPacket.rendererCreations != 1U) {
            releaseIfNeeded(packet);
            logError("Viewport native bridge smoke did not expose runtime stats v4 before release.");
            return false;
        }

        EditorViewportNativePresentPacket backpressuredPacket{};
        const std::uint32_t backpressuredStatus =
            editor_viewport_acquire_present_packet(&firstPresentRequest, &backpressuredPacket);
        const bool acquireRejectedWhilePending =
            backpressuredStatus == EditorViewportNativeStatus_Unavailable &&
            backpressuredPacket.status == EditorViewportNativeStatus_Unavailable &&
            backpressuredPacket.nativePacket == nullptr &&
            backpressuredPacket.imageHandle == nullptr &&
            backpressuredPacket.waitSemaphoreHandle == nullptr &&
            backpressuredPacket.signalSemaphoreHandle == nullptr;
        if (!acquireRejectedWhilePending) {
            releaseIfNeeded(backpressuredPacket);
            releaseIfNeeded(packet);
            logError("Viewport native bridge smoke allowed acquire while a present packet was "
                     "still pending.");
            return false;
        }

        EditorViewportNativeRuntimeStatsV5 statsV5AfterBackpressure{};
        if (!queryRuntimeStatsV5(statsV5AfterBackpressure) ||
            statsV5AfterBackpressure.framesRendered != 1U ||
            statsV5AfterBackpressure.packetsCreated != 1U ||
            statsV5AfterBackpressure.outstandingPackets != 1U ||
            statsV5AfterBackpressure.rendererCreations != 1U ||
            statsV5AfterBackpressure.maxOutstandingPackets != 1U ||
            statsV5AfterBackpressure.packetBackpressureHits != 1U ||
            statsV5AfterBackpressure.frameEpochsSubmitted != 1U ||
            statsV5AfterBackpressure.frameEpochsCompleted != 0U ||
            statsV5AfterBackpressure.frameEpochsPending != 1U) {
            releaseIfNeeded(backpressuredPacket);
            releaseIfNeeded(packet);
            logError("Viewport native bridge smoke did not expose v5 backpressure stats.");
            return false;
        }
        releaseIfNeeded(backpressuredPacket);
        releaseIfNeeded(packet);

        EditorViewportNativeRuntimeStatsV3 statsV3AfterFirstRelease{};
        if (!queryRuntimeStatsV3(statsV3AfterFirstRelease) ||
            statsV3AfterFirstRelease.frameEpochsSubmitted != 1U ||
            statsV3AfterFirstRelease.frameEpochsCompleted != 1U ||
            statsV3AfterFirstRelease.frameEpochsPending != 0U ||
            statsV3AfterFirstRelease.outstandingPackets != 0U) {
            logError("Viewport native bridge smoke did not expose completed epoch stats after first release.");
            return false;
        }
        EditorViewportNativeRuntimeStatsV4 statsV4AfterFirstRelease{};
        if (!queryRuntimeStatsV4(statsV4AfterFirstRelease) ||
            statsV4AfterFirstRelease.rendererCreations != 1U ||
            statsV4AfterFirstRelease.frameEpochsSubmitted != 1U ||
            statsV4AfterFirstRelease.frameEpochsCompleted != 1U ||
            statsV4AfterFirstRelease.frameEpochsPending != 0U ||
            statsV4AfterFirstRelease.outstandingPackets != 0U) {
            logError("Viewport native bridge smoke did not preserve renderer reuse stats after first release.");
            return false;
        }
        EditorViewportNativeRuntimeStatsV5 statsV5AfterFirstRelease{};
        if (!queryRuntimeStatsV5(statsV5AfterFirstRelease) ||
            statsV5AfterFirstRelease.rendererCreations != 1U ||
            statsV5AfterFirstRelease.packetsCreated != 1U ||
            statsV5AfterFirstRelease.outstandingPackets != 0U ||
            statsV5AfterFirstRelease.maxOutstandingPackets != 1U ||
            statsV5AfterFirstRelease.packetBackpressureHits != 1U ||
            statsV5AfterFirstRelease.frameEpochsSubmitted != 1U ||
            statsV5AfterFirstRelease.frameEpochsCompleted != 1U ||
            statsV5AfterFirstRelease.frameEpochsPending != 0U) {
            logError("Viewport native bridge smoke did not preserve v5 stats after first release.");
            return false;
        }

        EditorViewportNativePresentPacket secondPacket{};
        EditorViewportNativePresentRequest secondPresentRequest =
            makePresentRequest(VkExtent2D{.width = 320U, .height = 180U});
        const std::uint32_t secondPacketStatus =
            editor_viewport_acquire_present_packet(&secondPresentRequest, &secondPacket);
        const bool secondPacketAvailable =
            secondPacketStatus == EditorViewportNativeStatus_Success &&
            secondPacket.status == EditorViewportNativeStatus_Success &&
            secondPacket.nativePacket != nullptr && secondPacket.imageHandle != nullptr &&
            secondPacket.waitSemaphoreHandle != nullptr &&
            secondPacket.signalSemaphoreHandle != nullptr && secondPacket.widthPixels == 320U &&
            secondPacket.heightPixels == 180U && secondPacket.frameIndex == 2U;
        if (!secondPacketAvailable) {
            logPresentPacketMessage(secondPacket);
            releaseIfNeeded(secondPacket);
            logError("Viewport native bridge smoke did not produce the second same-size packet.");
            return false;
        }
        releaseIfNeeded(secondPacket);

        EditorViewportNativeRuntimeStatsV3 statsV3AfterSecondRelease{};
        if (!queryRuntimeStatsV3(statsV3AfterSecondRelease) ||
            statsV3AfterSecondRelease.frameEpochsSubmitted != 2U ||
            statsV3AfterSecondRelease.frameEpochsCompleted != 2U ||
            statsV3AfterSecondRelease.frameEpochsPending != 0U) {
            logError("Viewport native bridge smoke did not advance epoch stats after the second release.");
            return false;
        }
        EditorViewportNativeRuntimeStatsV4 statsV4AfterSecondRelease{};
        if (!queryRuntimeStatsV4(statsV4AfterSecondRelease) ||
            statsV4AfterSecondRelease.rendererCreations != 1U ||
            statsV4AfterSecondRelease.packetsCreated != 2U ||
            statsV4AfterSecondRelease.frameEpochsSubmitted != 2U ||
            statsV4AfterSecondRelease.frameEpochsCompleted != 2U ||
            statsV4AfterSecondRelease.frameEpochsPending != 0U) {
            logError("Viewport native bridge smoke did not preserve renderer reuse stats after the second release.");
            return false;
        }

        EditorViewportNativeRuntimeStatsV2 statsAfterSameSizeReuse{};
        if (!queryRuntimeStatsV2(statsAfterSameSizeReuse) ||
            statsAfterSameSizeReuse.externalImagesAcquired != 2U ||
            statsAfterSameSizeReuse.externalImagesCreated != 1U ||
            statsAfterSameSizeReuse.externalImagesReused < 1U ||
            statsAfterSameSizeReuse.externalImagesReleased < 2U ||
            statsAfterSameSizeReuse.externalImagesAvailable < 1U ||
            statsAfterSameSizeReuse.externalImagesLeased != 0U) {
            logError("Viewport native bridge smoke did not observe same-size external image reuse.");
            return false;
        }

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
            resizedPacket.heightPixels == 360U && resizedPacket.frameIndex == 3U;
        if (!resizedPacketAvailable) {
            logPresentPacketMessage(resizedPacket);
            releaseIfNeeded(resizedPacket);
            logError("Viewport native bridge smoke did not produce a resized shared present packet.");
            return false;
        }
        releaseIfNeeded(resizedPacket);

        EditorViewportNativeRuntimeStatsV3 statsV3AfterResizeRelease{};
        if (!queryRuntimeStatsV3(statsV3AfterResizeRelease) ||
            statsV3AfterResizeRelease.frameEpochsSubmitted != 3U ||
            statsV3AfterResizeRelease.frameEpochsCompleted != 3U ||
            statsV3AfterResizeRelease.frameEpochsPending != 0U) {
            logError("Viewport native bridge smoke did not advance epoch stats after the resized release.");
            return false;
        }
        EditorViewportNativeRuntimeStatsV4 statsV4AfterResizeRelease{};
        if (!queryRuntimeStatsV4(statsV4AfterResizeRelease) ||
            statsV4AfterResizeRelease.rendererCreations != 1U ||
            statsV4AfterResizeRelease.packetsCreated != 3U ||
            statsV4AfterResizeRelease.frameEpochsSubmitted != 3U ||
            statsV4AfterResizeRelease.frameEpochsCompleted != 3U ||
            statsV4AfterResizeRelease.frameEpochsPending != 0U) {
            logError("Viewport native bridge smoke did not preserve renderer reuse stats after the resized release.");
            return false;
        }

        EditorViewportNativeRuntimeStatsV2 statsAfterResize{};
        if (!queryRuntimeStatsV2(statsAfterResize) ||
            statsAfterResize.externalImagesAcquired != 3U ||
            statsAfterResize.externalImagesCreated != 2U ||
            statsAfterResize.externalImagesReused < 1U ||
            statsAfterResize.externalImagesReleased < 3U ||
            statsAfterResize.externalImagesAvailable < 2U ||
            statsAfterResize.externalImagesLeased != 0U) {
            logError("Viewport native bridge smoke did not observe resize external image allocation.");
            return false;
        }

        EditorViewportNativePresentPacket shutdownPendingPacket{};
        EditorViewportNativePresentRequest shutdownPendingRequest =
            makePresentRequest(VkExtent2D{.width = 160U, .height = 90U});
        const std::uint32_t shutdownPendingStatus =
            editor_viewport_acquire_present_packet(&shutdownPendingRequest, &shutdownPendingPacket);
        const bool shutdownPendingPacketAvailable =
            shutdownPendingStatus == EditorViewportNativeStatus_Success &&
            shutdownPendingPacket.status == EditorViewportNativeStatus_Success &&
            shutdownPendingPacket.nativePacket != nullptr &&
            shutdownPendingPacket.imageHandle != nullptr &&
            shutdownPendingPacket.waitSemaphoreHandle != nullptr &&
            shutdownPendingPacket.signalSemaphoreHandle != nullptr &&
            shutdownPendingPacket.frameIndex == 4U;
        if (!shutdownPendingPacketAvailable) {
            logPresentPacketMessage(shutdownPendingPacket);
            releaseIfNeeded(shutdownPendingPacket);
            logError("Viewport native bridge smoke did not produce a packet for shutdown ordering.");
            return false;
        }

        EditorViewportNativeRuntimeStatsV3 statsV3BeforeShutdown{};
        if (!queryRuntimeStatsV3(statsV3BeforeShutdown) ||
            statsV3BeforeShutdown.frameEpochsSubmitted != 4U ||
            statsV3BeforeShutdown.frameEpochsCompleted != 3U ||
            statsV3BeforeShutdown.frameEpochsPending != 1U ||
            statsV3BeforeShutdown.outstandingPackets != 1U) {
            releaseIfNeeded(shutdownPendingPacket);
            logError("Viewport native bridge smoke did not preserve pending epoch stats before shutdown.");
            return false;
        }
        EditorViewportNativeRuntimeStatsV4 statsV4BeforeShutdown{};
        if (!queryRuntimeStatsV4(statsV4BeforeShutdown) ||
            statsV4BeforeShutdown.rendererCreations != 1U ||
            statsV4BeforeShutdown.packetsCreated != 4U ||
            statsV4BeforeShutdown.frameEpochsSubmitted != 4U ||
            statsV4BeforeShutdown.frameEpochsCompleted != 3U ||
            statsV4BeforeShutdown.frameEpochsPending != 1U ||
            statsV4BeforeShutdown.outstandingPackets != 1U) {
            releaseIfNeeded(shutdownPendingPacket);
            logError("Viewport native bridge smoke did not preserve renderer reuse stats before shutdown.");
            return false;
        }

        editor_viewport_shutdown();
        releaseIfNeeded(shutdownPendingPacket);

        EditorViewportNativePresentPacket afterShutdownPacket{};
        EditorViewportNativePresentRequest afterShutdownRequest =
            makePresentRequest(VkExtent2D{.width = 160U, .height = 90U});
        const std::uint32_t afterShutdownStatus =
            editor_viewport_acquire_present_packet(&afterShutdownRequest, &afterShutdownPacket);
        const bool acquireRejectedAfterShutdown =
            afterShutdownStatus == EditorViewportNativeStatus_Unavailable &&
            afterShutdownPacket.status == EditorViewportNativeStatus_Unavailable &&
            afterShutdownPacket.nativePacket == nullptr;
        releaseIfNeeded(afterShutdownPacket);
        if (!acquireRejectedAfterShutdown) {
            logError("Viewport native bridge smoke allowed acquire after viewport shutdown.");
            return false;
        }

        return true;
    }

} // namespace asharia::editor
