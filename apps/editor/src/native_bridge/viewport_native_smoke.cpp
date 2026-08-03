#include "native_bridge/viewport_native_smoke.hpp"

#include <vulkan/vulkan.h>

#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <vulkan/vulkan_win32.h>
// clang-format on

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>
#include <type_traits>

#include "asharia/core/log.hpp"
#include "asharia/rhi_vulkan/vulkan_context.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

#include "editor_shared_viewport_render_producer.hpp"
#include "editor_shared_viewport_runtime.hpp"
#include "native_bridge/viewport_native_api.hpp"

namespace asharia::editor {
    namespace {

        static_assert(
            std::is_same_v<decltype(EditorSharedViewportRuntime::instance().ensureDeviceSnapshot()),
                           asharia::Result<EditorSharedViewportDeviceSnapshot>>);

        void logNativeMessage(const void* messageUtf8, std::uint64_t messageByteLength) {
            if (messageUtf8 == nullptr || messageByteLength == 0U) {
                return;
            }

            const auto message =
                std::string_view{static_cast<const char*>(messageUtf8), messageByteLength};
            logError(message);
        }

        void logPresentPacketMessage(const EditorViewportNativePresentPacket& packet) {
            logNativeMessage(packet.messageUtf8, packet.messageByteLength);
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

        [[nodiscard]] EditorViewportNativePresentRequestV2
        makePresentRequestV2(VkExtent2D extent, bool hasScene, std::uint64_t sceneRevision) {
            return EditorViewportNativePresentRequestV2{
                .header =
                    EditorViewportNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize = static_cast<std::uint32_t>(
                            sizeof(EditorViewportNativePresentRequestV2)),
                    },
                .compatibility = makeRequest(),
                .widthPixels = extent.width,
                .heightPixels = extent.height,
                .hasScene = hasScene ? 1U : 0U,
                .reserved = 0U,
                .sceneRevision = hasScene ? sceneRevision : 0U,
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

        struct ImportedCompositionSemaphores final {
            ImportedCompositionSemaphores() = default;
            ImportedCompositionSemaphores(const ImportedCompositionSemaphores&) = delete;
            ImportedCompositionSemaphores& operator=(const ImportedCompositionSemaphores&) = delete;
            ImportedCompositionSemaphores(ImportedCompositionSemaphores&&) = delete;
            ImportedCompositionSemaphores& operator=(ImportedCompositionSemaphores&&) = delete;

            ~ImportedCompositionSemaphores() {
                if (ready != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, ready, nullptr);
                }
                if (release != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device, release, nullptr);
                }
            }

            VkDevice device{VK_NULL_HANDLE};
            VkSemaphore ready{VK_NULL_HANDLE};
            VkSemaphore release{VK_NULL_HANDLE};
        };

        [[nodiscard]] PFN_vkImportSemaphoreWin32HandleKHR
        loadImportSemaphoreWin32Handle(VkDevice device) {
            // Vulkan extension entry points use the API's generic function-pointer lookup.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<PFN_vkImportSemaphoreWin32HandleKHR>(
                vkGetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR"));
        }

        [[nodiscard]] bool
        importCompositionSemaphore(VkDevice device,
                                   PFN_vkImportSemaphoreWin32HandleKHR importSemaphore,
                                   void* sourceHandle, VkSemaphore& semaphore) {
            VkSemaphoreCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            const VkResult created = vkCreateSemaphore(device, &createInfo, nullptr, &semaphore);
            if (created != VK_SUCCESS) {
                return false;
            }

            VkImportSemaphoreWin32HandleInfoKHR importInfo{};
            importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
            importInfo.semaphore = semaphore;
            importInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            importInfo.handle = static_cast<HANDLE>(sourceHandle);
            return importSemaphore(device, &importInfo) == VK_SUCCESS;
        }

        [[nodiscard]] bool
        completeCompositionCycle(const VulkanContext& context,
                                 const EditorViewportNativePresentPacket& packet) {
            if (packet.nativePacket == nullptr || packet.waitSemaphoreHandle == nullptr ||
                packet.signalSemaphoreHandle == nullptr) {
                return false;
            }

            const PFN_vkImportSemaphoreWin32HandleKHR importSemaphore =
                loadImportSemaphoreWin32Handle(context.device());
            if (importSemaphore == nullptr) {
                logError("Viewport native bridge smoke could not load semaphore import.");
                return false;
            }

            ImportedCompositionSemaphores semaphores;
            semaphores.device = context.device();
            if (!importCompositionSemaphore(context.device(), importSemaphore,
                                            packet.waitSemaphoreHandle, semaphores.ready) ||
                !importCompositionSemaphore(context.device(), importSemaphore,
                                            packet.signalSemaphoreHandle, semaphores.release)) {
                logError("Viewport native bridge smoke could not import composition semaphores.");
                return false;
            }

            VkFence fence = VK_NULL_HANDLE;
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkResult result = vkCreateFence(context.device(), &fenceInfo, nullptr, &fence);
            if (result != VK_SUCCESS) {
                logError("Viewport native bridge smoke could not create a composition fence.");
                return false;
            }

            VkSemaphoreSubmitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfo.semaphore = semaphores.ready;
            waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSemaphoreSubmitInfo signalInfo{};
            signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfo.semaphore = semaphores.release;
            signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.waitSemaphoreInfoCount = 1U;
            submitInfo.pWaitSemaphoreInfos = &waitInfo;
            submitInfo.signalSemaphoreInfoCount = 1U;
            submitInfo.pSignalSemaphoreInfos = &signalInfo;

            result = vkQueueSubmit2(context.graphicsQueue(), 1U, &submitInfo, fence);
            if (result == VK_SUCCESS) {
                constexpr std::uint64_t kCompositionTimeoutNanoseconds = 5'000'000'000ULL;
                result = vkWaitForFences(context.device(), 1U, &fence, VK_TRUE,
                                         kCompositionTimeoutNanoseconds);
            }
            vkDestroyFence(context.device(), fence, nullptr);
            if (result != VK_SUCCESS) {
                logError("Viewport native bridge smoke did not complete the composition cycle.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        expectCompatibilityStatus(const EditorViewportNativeCompatibilityRequest* request,
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

        [[nodiscard]] bool queryRuntimeStatsV6(EditorViewportNativeRuntimeStatsV6& stats) {
            const std::uint32_t status = editor_viewport_query_runtime_stats_v6(&stats);
            return status == EditorViewportNativeStatus_Success &&
                   stats.header.abiVersion == EDITOR_NATIVE_ABI_VERSION &&
                   stats.header.structSize == sizeof(EditorViewportNativeRuntimeStatsV6);
        }

        [[nodiscard]] bool waitForRuntimeEpochs(std::uint64_t submitted, std::uint64_t completed,
                                                std::uint64_t pending,
                                                std::uint64_t outstandingPackets) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                EditorViewportNativeRuntimeStatsV3 stats{};
                if (queryRuntimeStatsV3(stats) && stats.frameEpochsSubmitted == submitted &&
                    stats.frameEpochsCompleted == completed &&
                    stats.frameEpochsPending == pending &&
                    stats.outstandingPackets == outstandingPackets) {
                    return true;
                }
                std::this_thread::yield();
            }
            return false;
        }

        [[nodiscard]] bool waitForExternalImageLeases(std::uint64_t expectedLeases) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                EditorViewportNativeRuntimeStatsV2 stats{};
                if (queryRuntimeStatsV2(stats) && stats.externalImagesLeased == expectedLeases) {
                    return true;
                }
                std::this_thread::yield();
            }
            return false;
        }

        [[nodiscard]] bool waitForRuntimeShutdown() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                EditorViewportNativeRuntimeStatsV2 stats{};
                if (queryRuntimeStatsV2(stats) && stats.hasContext == 0U &&
                    stats.hasRenderProducer == 0U && stats.shutdownRequested == 1U &&
                    stats.outstandingPackets == 0U) {
                    return true;
                }
                std::this_thread::yield();
            }
            return false;
        }

        [[nodiscard]] bool smokeCompatibilityContract() {
            if (!expectCompatibilityStatus(nullptr, EditorViewportNativeStatus_InvalidArgument)) {
                logError(
                    "Viewport native bridge smoke did not reject a null compatibility request.");
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
                logError(
                    "Viewport native bridge smoke did not reject an unknown image handle type.");
                return false;
            }

            EditorViewportNativeCompatibilityRequest supportedRequest = makeRequest();
            EditorViewportNativeCompatibilityResult supportedResult{};
            const std::uint32_t supportedStatus = editor_viewport_query_composition_compatibility(
                &supportedRequest, &supportedResult);
            const bool supported = supportedStatus == EditorViewportNativeStatus_Success &&
                                   supportedResult.status == EditorViewportNativeStatus_Success &&
                                   supportedResult.producedImageHandleType ==
                                       EditorViewportNativeHandleType_VulkanOpaqueNt &&
                                   supportedResult.producedSemaphoreHandleType ==
                                       EditorViewportNativeHandleType_VulkanOpaqueNt;
            if (!supported) {
                logNativeMessage(supportedResult.messageUtf8, supportedResult.messageByteLength);
                releaseIfNeeded(supportedResult);
                logError("Viewport native bridge smoke did not accept a Vulkan opaque NT request.");
                return false;
            }
            EditorViewportNativeCompatibilityRequest matchingRequest = makeRequest();
            matchingRequest.hasDeviceUuid = 1U;
            matchingRequest.deviceUuidLow = supportedResult.nativeDeviceUuidLow;
            matchingRequest.deviceUuidHigh = supportedResult.nativeDeviceUuidHigh;
            releaseIfNeeded(supportedResult);

            if (!expectCompatibilityStatus(&matchingRequest, EditorViewportNativeStatus_Success)) {
                logError("Viewport native bridge smoke did not match its device snapshot UUID.");
                return false;
            }

            EditorViewportNativeCompatibilityRequest mismatchedRequest =
                makeMismatchedUuidRequest();
            if (!expectCompatibilityStatus(&mismatchedRequest,
                                           EditorViewportNativeStatus_DeviceMismatch)) {
                logError("Viewport native bridge smoke did not detect a mismatched device UUID.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool smokeFirstPacketAndBackpressure() {
            EditorViewportNativePresentRequestV2 invalidSceneRequest =
                makePresentRequestV2(VkExtent2D{.width = 320U, .height = 180U}, false, 0U);
            invalidSceneRequest.sceneRevision = 1U;
            EditorViewportNativePresentPacket invalidScenePacket{};
            if (editor_viewport_acquire_present_packet_v2(&invalidSceneRequest,
                                                          &invalidScenePacket) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                invalidScenePacket.status != EditorViewportNativeStatus_InvalidArgument) {
                releaseIfNeeded(invalidScenePacket);
                logError("Viewport native bridge smoke accepted a scene revision without a "
                         "scene.");
                return false;
            }

            EditorViewportNativePresentPacket packet{};
            EditorViewportNativePresentRequestV2 firstPresentRequest =
                makePresentRequestV2(VkExtent2D{.width = 320U, .height = 180U}, true, 42U);
            const std::uint32_t packetStatus =
                editor_viewport_acquire_present_packet_v2(&firstPresentRequest, &packet);
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
                logError("Viewport native bridge smoke did not produce the first shared present "
                         "packet.");
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
                logError(
                    "Viewport native bridge smoke did not expose first render producer stats.");
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
                logError(
                    "Viewport native bridge smoke did not expose runtime stats v3 before release.");
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
                logError(
                    "Viewport native bridge smoke did not expose runtime stats v4 before release.");
                return false;
            }
            EditorViewportNativeRuntimeStatsV6 statsV6AfterFirstPacket{};
            if (!queryRuntimeStatsV6(statsV6AfterFirstPacket) ||
                statsV6AfterFirstPacket.framesRendered != 1U ||
                statsV6AfterFirstPacket.sceneFramesRendered != 1U ||
                statsV6AfterFirstPacket.lastSceneRevision != 42U) {
                releaseIfNeeded(packet);
                logError("Viewport native bridge smoke did not consume the minimal scene "
                         "revision.");
                return false;
            }

            EditorViewportNativePresentPacket backpressuredPacket{};
            const std::uint32_t backpressuredStatus = editor_viewport_acquire_present_packet_v2(
                &firstPresentRequest, &backpressuredPacket);
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
                statsV5AfterBackpressure.maxOutstandingPackets != 4U ||
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
            if (!waitForRuntimeEpochs(1U, 1U, 0U, 0U)) {
                logError("Viewport native bridge smoke did not poll the first packet retirement.");
                return false;
            }

            EditorViewportNativeRuntimeStatsV3 statsV3AfterFirstRelease{};
            if (!queryRuntimeStatsV3(statsV3AfterFirstRelease) ||
                statsV3AfterFirstRelease.frameEpochsSubmitted != 1U ||
                statsV3AfterFirstRelease.frameEpochsCompleted != 1U ||
                statsV3AfterFirstRelease.frameEpochsPending != 0U ||
                statsV3AfterFirstRelease.outstandingPackets != 0U) {
                logError("Viewport native bridge smoke did not expose completed epoch stats after "
                         "first release.");
                return false;
            }
            EditorViewportNativeRuntimeStatsV4 statsV4AfterFirstRelease{};
            if (!queryRuntimeStatsV4(statsV4AfterFirstRelease) ||
                statsV4AfterFirstRelease.rendererCreations != 1U ||
                statsV4AfterFirstRelease.frameEpochsSubmitted != 1U ||
                statsV4AfterFirstRelease.frameEpochsCompleted != 1U ||
                statsV4AfterFirstRelease.frameEpochsPending != 0U ||
                statsV4AfterFirstRelease.outstandingPackets != 0U) {
                logError("Viewport native bridge smoke did not preserve renderer reuse stats after "
                         "first release.");
                return false;
            }
            EditorViewportNativeRuntimeStatsV5 statsV5AfterFirstRelease{};
            if (!queryRuntimeStatsV5(statsV5AfterFirstRelease) ||
                statsV5AfterFirstRelease.rendererCreations != 1U ||
                statsV5AfterFirstRelease.packetsCreated != 1U ||
                statsV5AfterFirstRelease.outstandingPackets != 0U ||
                statsV5AfterFirstRelease.maxOutstandingPackets != 4U ||
                statsV5AfterFirstRelease.packetBackpressureHits != 1U ||
                statsV5AfterFirstRelease.frameEpochsSubmitted != 1U ||
                statsV5AfterFirstRelease.frameEpochsCompleted != 1U ||
                statsV5AfterFirstRelease.frameEpochsPending != 0U) {
                logError(
                    "Viewport native bridge smoke did not preserve v5 stats after first release.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool smokeSameSizeLegacyReuse() {
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
                logError(
                    "Viewport native bridge smoke did not produce the second same-size packet.");
                return false;
            }
            releaseIfNeeded(secondPacket);
            if (!waitForRuntimeEpochs(2U, 2U, 0U, 0U)) {
                logError(
                    "Viewport native bridge smoke did not poll the same-size packet retirement.");
                return false;
            }

            EditorViewportNativeRuntimeStatsV3 statsV3AfterSecondRelease{};
            if (!queryRuntimeStatsV3(statsV3AfterSecondRelease) ||
                statsV3AfterSecondRelease.frameEpochsSubmitted != 2U ||
                statsV3AfterSecondRelease.frameEpochsCompleted != 2U ||
                statsV3AfterSecondRelease.frameEpochsPending != 0U) {
                logError("Viewport native bridge smoke did not advance epoch stats after the "
                         "second release.");
                return false;
            }
            EditorViewportNativeRuntimeStatsV4 statsV4AfterSecondRelease{};
            if (!queryRuntimeStatsV4(statsV4AfterSecondRelease) ||
                statsV4AfterSecondRelease.rendererCreations != 1U ||
                statsV4AfterSecondRelease.packetsCreated != 2U ||
                statsV4AfterSecondRelease.frameEpochsSubmitted != 2U ||
                statsV4AfterSecondRelease.frameEpochsCompleted != 2U ||
                statsV4AfterSecondRelease.frameEpochsPending != 0U) {
                logError("Viewport native bridge smoke did not preserve renderer reuse stats after "
                         "the second release.");
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
                logError(
                    "Viewport native bridge smoke did not observe same-size external image reuse.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool smokeResizeChurn() {
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
                resizedPacket.signalSemaphoreHandle != nullptr &&
                resizedPacket.widthPixels == 640U && resizedPacket.heightPixels == 360U &&
                resizedPacket.frameIndex == 3U;
            if (!resizedPacketAvailable) {
                logPresentPacketMessage(resizedPacket);
                releaseIfNeeded(resizedPacket);
                logError("Viewport native bridge smoke did not produce a resized shared present "
                         "packet.");
                return false;
            }
            releaseIfNeeded(resizedPacket);
            if (!waitForRuntimeEpochs(3U, 3U, 0U, 0U)) {
                logError(
                    "Viewport native bridge smoke did not poll the resized packet retirement.");
                return false;
            }

            EditorViewportNativeRuntimeStatsV3 statsV3AfterResizeRelease{};
            if (!queryRuntimeStatsV3(statsV3AfterResizeRelease) ||
                statsV3AfterResizeRelease.frameEpochsSubmitted != 3U ||
                statsV3AfterResizeRelease.frameEpochsCompleted != 3U ||
                statsV3AfterResizeRelease.frameEpochsPending != 0U) {
                logError("Viewport native bridge smoke did not advance epoch stats after the "
                         "resized release.");
                return false;
            }
            EditorViewportNativeRuntimeStatsV4 statsV4AfterResizeRelease{};
            if (!queryRuntimeStatsV4(statsV4AfterResizeRelease) ||
                statsV4AfterResizeRelease.rendererCreations != 1U ||
                statsV4AfterResizeRelease.packetsCreated != 3U ||
                statsV4AfterResizeRelease.frameEpochsSubmitted != 3U ||
                statsV4AfterResizeRelease.frameEpochsCompleted != 3U ||
                statsV4AfterResizeRelease.frameEpochsPending != 0U) {
                logError("Viewport native bridge smoke did not preserve renderer reuse stats after "
                         "the resized release.");
                return false;
            }

            EditorViewportNativeRuntimeStatsV2 statsAfterResize{};
            if (!queryRuntimeStatsV2(statsAfterResize) ||
                statsAfterResize.externalImagesAcquired != 3U ||
                statsAfterResize.externalImagesCreated != 2U ||
                statsAfterResize.externalImagesReused < 1U ||
                statsAfterResize.externalImagesReleased < 3U ||
                statsAfterResize.externalImagesAvailable != 2U ||
                statsAfterResize.externalImagesLeased != 0U) {
                logError("Viewport native bridge smoke did not observe resize external image "
                         "allocation.");
                return false;
            }

            EditorViewportNativePresentPacket resizeChurnPacket{};
            EditorViewportNativePresentRequest resizeChurnRequest =
                makePresentRequest(VkExtent2D{.width = 800U, .height = 450U});
            const std::uint32_t resizeChurnStatus =
                editor_viewport_acquire_present_packet(&resizeChurnRequest, &resizeChurnPacket);
            const bool resizeChurnPacketAvailable =
                resizeChurnStatus == EditorViewportNativeStatus_Success &&
                resizeChurnPacket.status == EditorViewportNativeStatus_Success &&
                resizeChurnPacket.nativePacket != nullptr &&
                resizeChurnPacket.widthPixels == 800U && resizeChurnPacket.heightPixels == 450U &&
                resizeChurnPacket.frameIndex == 4U;
            if (!resizeChurnPacketAvailable) {
                logPresentPacketMessage(resizeChurnPacket);
                releaseIfNeeded(resizeChurnPacket);
                logError("Viewport native bridge smoke did not produce the resize churn packet.");
                return false;
            }
            releaseIfNeeded(resizeChurnPacket);
            if (!waitForRuntimeEpochs(4U, 4U, 0U, 0U)) {
                logError("Viewport native bridge smoke did not poll the resize churn retirement.");
                return false;
            }

            EditorViewportNativeRuntimeStatsV2 statsAfterResizeChurn{};
            if (!queryRuntimeStatsV2(statsAfterResizeChurn) ||
                statsAfterResizeChurn.externalImagesAcquired != 4U ||
                statsAfterResizeChurn.externalImagesCreated != 3U ||
                statsAfterResizeChurn.externalImagesReleased < 4U ||
                statsAfterResizeChurn.externalImagesAvailable != 2U ||
                statsAfterResizeChurn.externalImagesLeased != 0U) {
                logError("Viewport native bridge smoke observed an unbounded resize image cache.");
                return false;
            }

            return true;
        }

        using AdditionalPresentSlots = std::array<EditorViewportNativePresentPacket, 3U>;

        void releaseAll(AdditionalPresentSlots& slots) {
            for (EditorViewportNativePresentPacket& slot : slots) {
                releaseIfNeeded(slot);
            }
        }

        [[nodiscard]] bool createReusableSlot(const EditorViewportNativePresentRequestV2& request,
                                              EditorViewportNativePresentPacket& slot) {
            const std::uint32_t status = editor_viewport_create_present_slot_v3(&request, &slot);
            if (status == EditorViewportNativeStatus_Success &&
                slot.status == EditorViewportNativeStatus_Success && slot.nativePacket != nullptr &&
                slot.frameIndex == 5U) {
                return true;
            }

            logPresentPacketMessage(slot);
            releaseIfNeeded(slot);
            logError("Viewport native bridge smoke did not create a reusable present slot.");
            return false;
        }

        [[nodiscard]] bool smokeReusableSlotFrames(const VulkanContext& compositionContext,
                                                   EditorViewportNativePresentPacket& slot) {
            EditorViewportNativePresentSlotRenderRequest renderRequest{
                .header =
                    EditorViewportNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize = static_cast<std::uint32_t>(
                            sizeof(EditorViewportNativePresentSlotRenderRequest)),
                    },
                .nativeSlot = slot.nativePacket,
                .widthPixels = slot.widthPixels,
                .heightPixels = slot.heightPixels,
                .hasScene = 1U,
                .reserved = 0U,
                .sceneRevision = 10U,
            };

            if (!completeCompositionCycle(compositionContext, slot) ||
                editor_viewport_render_present_slot_v3(&renderRequest, &slot) !=
                    EditorViewportNativeStatus_Success ||
                slot.frameIndex != 6U || !completeCompositionCycle(compositionContext, slot)) {
                logError("Viewport native bridge smoke did not reuse a present slot.");
                return false;
            }

            renderRequest.sceneRevision = 11U;
            if (editor_viewport_render_present_slot_v3(&renderRequest, &slot) !=
                    EditorViewportNativeStatus_Success ||
                slot.frameIndex != 7U || !completeCompositionCycle(compositionContext, slot)) {
                logError("Viewport native bridge smoke did not repeatedly reuse a present slot.");
                return false;
            }

            EditorViewportNativePresentSlotRenderRequest mismatchedExtentRender = renderRequest;
            ++mismatchedExtentRender.widthPixels;
            if (editor_viewport_render_present_slot_v3(&mismatchedExtentRender, &slot) !=
                EditorViewportNativeStatus_InvalidArgument) {
                logError("Viewport native bridge smoke changed a present slot extent in place.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool
        smokeBoundedReusableSlots(const EditorViewportNativePresentRequestV2& request) {
            AdditionalPresentSlots additionalSlots{};
            for (EditorViewportNativePresentPacket& slot : additionalSlots) {
                const std::uint32_t status =
                    editor_viewport_create_present_slot_v3(&request, &slot);
                if (status != EditorViewportNativeStatus_Success ||
                    slot.status != EditorViewportNativeStatus_Success ||
                    slot.nativePacket == nullptr) {
                    releaseAll(additionalSlots);
                    logError("Viewport native bridge smoke did not allocate four bounded slots.");
                    return false;
                }
            }

            EditorViewportNativePresentPacket slotBeyondLimit{};
            const std::uint32_t status =
                editor_viewport_create_present_slot_v3(&request, &slotBeyondLimit);
            const bool limitEnforced =
                status == EditorViewportNativeStatus_Unavailable &&
                slotBeyondLimit.status == EditorViewportNativeStatus_Unavailable &&
                slotBeyondLimit.nativePacket == nullptr;
            releaseIfNeeded(slotBeyondLimit);
            releaseAll(additionalSlots);
            if (!limitEnforced) {
                logError("Viewport native bridge smoke exceeded the four-slot limit.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool smokeReusableSlotStats() {
            EditorViewportNativeRuntimeStatsV5 stats{};
            if (!waitForRuntimeEpochs(10U, 10U, 0U, 0U) || !queryRuntimeStatsV5(stats) ||
                stats.framesRendered != 10U || stats.packetsCreated != 8U ||
                stats.outstandingPackets != 0U || stats.maxOutstandingPackets != 4U ||
                stats.packetBackpressureHits != 2U || stats.frameEpochsSubmitted != 10U ||
                stats.frameEpochsCompleted != 10U || stats.frameEpochsPending != 0U) {
                logError("Viewport native bridge smoke did not retire reusable slots cleanly.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool
        smokeNonBlockingRetirement(const VulkanContext& compositionContext,
                                   const EditorViewportNativePresentRequestV2& request) {
            EditorViewportNativeRuntimeStatsV5 statsBefore{};
            if (!queryRuntimeStatsV5(statsBefore) || statsBefore.frameEpochsPending != 0U ||
                statsBefore.outstandingPackets != 0U) {
                logError(
                    "Viewport native bridge smoke did not start retirement from an idle runtime.");
                return false;
            }

            EditorViewportNativePresentPacket slot{};
            if (editor_viewport_create_present_slot_v3(&request, &slot) !=
                    EditorViewportNativeStatus_Success ||
                slot.status != EditorViewportNativeStatus_Success || slot.nativePacket == nullptr ||
                slot.frameIndex != statsBefore.framesRendered + 1U) {
                logPresentPacketMessage(slot);
                releaseIfNeeded(slot);
                logError("Viewport native bridge smoke could not create the retirement test slot.");
                return false;
            }

            auto* state = static_cast<EditorSharedViewportPacketState*>(slot.nativePacket);
            if (state->device == VK_NULL_HANDLE || state->fence == VK_NULL_HANDLE ||
                !state->submitted) {
                releaseIfNeeded(slot);
                logError("Viewport native bridge smoke received an incomplete retirement slot.");
                return false;
            }

            VkQueue graphicsQueue = VK_NULL_HANDLE;
            // Both smoke contexts use the same no-surface queue policy; the preceding interop
            // checks establish that this family is valid for the packet device.
            vkGetDeviceQueue(state->device, compositionContext.graphicsQueueFamily(), 0U,
                             &graphicsQueue);

            constexpr std::uint64_t kInitialFrameTimeoutNanoseconds = 5'000'000'000ULL;
            VkResult setupResult = vkWaitForFences(state->device, 1U, &state->fence, VK_TRUE,
                                                   kInitialFrameTimeoutNanoseconds);
            if (setupResult != VK_SUCCESS) {
                releaseIfNeeded(slot);
                logError(
                    "Viewport native bridge smoke could not observe the initial frame completion.");
                return false;
            }

            setupResult = vkResetFences(state->device, 1U, &state->fence);
            if (setupResult != VK_SUCCESS || graphicsQueue == VK_NULL_HANDLE) {
                state->submitted = false;
                releaseIfNeeded(slot);
                logError("Viewport native bridge smoke could not reset its retirement fence.");
                return false;
            }

            const VkFence retirementFence = state->fence;
            const auto releaseStarted = std::chrono::steady_clock::now();
            releaseIfNeeded(slot);
            slot = {};
            const auto releaseDuration = std::chrono::steady_clock::now() - releaseStarted;
            constexpr auto kMaximumNonBlockingReleaseDuration = std::chrono::milliseconds{500};
            bool passed = releaseDuration < kMaximumNonBlockingReleaseDuration;
            if (!passed) {
                logError("Viewport native bridge smoke observed a blocking packet release.");
            }

            const std::uint64_t expectedSubmitted = statsBefore.frameEpochsSubmitted + 1U;
            const std::uint64_t expectedCompleted = statsBefore.frameEpochsCompleted;
            EditorViewportNativeRuntimeStatsV5 statsPending{};
            if (!queryRuntimeStatsV5(statsPending) ||
                statsPending.frameEpochsSubmitted != expectedSubmitted ||
                statsPending.frameEpochsCompleted != expectedCompleted ||
                statsPending.frameEpochsPending != 1U || statsPending.outstandingPackets != 0U) {
                logError("Viewport native bridge smoke did not retain pending work for polling.");
                passed = false;
            }

            EditorViewportNativeRuntimeStatsV2 statsPendingV2{};
            if (!queryRuntimeStatsV2(statsPendingV2) || statsPendingV2.externalImagesLeased != 1U) {
                logError("Viewport native bridge smoke released a pending external image early.");
                passed = false;
            }

            VkSubmitInfo2 completionSubmit{};
            completionSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            const VkResult completionSubmitted =
                vkQueueSubmit2(graphicsQueue, 1U, &completionSubmit, retirementFence);
            if (completionSubmitted != VK_SUCCESS) {
                logError(vulkanError("Viewport native bridge smoke could not submit retirement "
                                     "completion",
                                     completionSubmitted)
                             .message);
                return false;
            }

            const bool epochsReclaimed =
                waitForRuntimeEpochs(expectedSubmitted, expectedSubmitted, 0U, 0U);
            const bool leaseReturned = waitForExternalImageLeases(0U);
            if (!epochsReclaimed || !leaseReturned) {
                // Debug-smoke cleanup only: no render loop can reach this fallback.
                const VkResult queueIdle = vkQueueWaitIdle(graphicsQueue);
                if (queueIdle != VK_SUCCESS) {
                    logError(vulkanError("Viewport native bridge smoke could not idle the "
                                         "retirement queue",
                                         queueIdle)
                                 .message);
                }
                EditorViewportNativeRuntimeStatsV2 finalPoll{};
                [[maybe_unused]] const bool finalPollSucceeded = queryRuntimeStatsV2(finalPoll);
            }

            if (!epochsReclaimed) {
                logError("Viewport native bridge smoke did not reclaim the polled retirement.");
                return false;
            }
            if (!leaseReturned) {
                logError("Viewport native bridge smoke did not return the retired image lease.");
                return false;
            }

            EditorViewportNativeRuntimeStatsV2 statsAfter{};
            if (!queryRuntimeStatsV2(statsAfter) || statsAfter.externalImagesLeased != 0U) {
                logError("Viewport native bridge smoke retained a reclaimed external image lease.");
                return false;
            }
            return passed;
        }

        [[nodiscard]] bool smokeReusableSlots() {
            auto compositionContext = VulkanContext::create(VulkanContextDesc{
                .applicationName = "Shared viewport composition smoke",
                .requiredInstanceExtensions = {},
                .createSurface = {},
                .enableValidation = false,
                .debugLabels = VulkanDebugLabelMode::Optional,
                .requireVulkan14 = true,
                .externalInterop =
                    VulkanExternalInteropOptions{
                        .opaqueWin32Semaphore = true,
                    },
            });
            if (!compositionContext) {
                logError(compositionContext.error().message);
                return false;
            }

            EditorViewportNativePresentPacket reusableSlot{};
            const EditorViewportNativePresentRequestV2 request =
                makePresentRequestV2(VkExtent2D{.width = 800U, .height = 450U}, true, 9U);
            if (!createReusableSlot(request, reusableSlot)) {
                return false;
            }

            const bool framesPassed = smokeReusableSlotFrames(*compositionContext, reusableSlot);
            const bool limitPassed = framesPassed && smokeBoundedReusableSlots(request);
            releaseIfNeeded(reusableSlot);
            const bool statsPassed = framesPassed && limitPassed && smokeReusableSlotStats();
            return statsPassed && smokeNonBlockingRetirement(*compositionContext, request);
        }

        [[nodiscard]] bool smokeShutdownOrdering() {
            EditorViewportNativePresentPacket shutdownPendingPacket{};
            EditorViewportNativePresentRequest shutdownPendingRequest =
                makePresentRequest(VkExtent2D{.width = 160U, .height = 90U});
            const std::uint32_t shutdownPendingStatus = editor_viewport_acquire_present_packet(
                &shutdownPendingRequest, &shutdownPendingPacket);
            const bool shutdownPendingPacketAvailable =
                shutdownPendingStatus == EditorViewportNativeStatus_Success &&
                shutdownPendingPacket.status == EditorViewportNativeStatus_Success &&
                shutdownPendingPacket.nativePacket != nullptr &&
                shutdownPendingPacket.imageHandle != nullptr &&
                shutdownPendingPacket.waitSemaphoreHandle != nullptr &&
                shutdownPendingPacket.signalSemaphoreHandle != nullptr &&
                shutdownPendingPacket.frameIndex == 12U;
            if (!shutdownPendingPacketAvailable) {
                logPresentPacketMessage(shutdownPendingPacket);
                releaseIfNeeded(shutdownPendingPacket);
                logError(
                    "Viewport native bridge smoke did not produce a packet for shutdown ordering.");
                return false;
            }

            EditorViewportNativeRuntimeStatsV3 statsV3BeforeShutdown{};
            if (!queryRuntimeStatsV3(statsV3BeforeShutdown) ||
                statsV3BeforeShutdown.frameEpochsSubmitted != 12U ||
                statsV3BeforeShutdown.frameEpochsCompleted != 11U ||
                statsV3BeforeShutdown.frameEpochsPending != 1U ||
                statsV3BeforeShutdown.outstandingPackets != 1U) {
                releaseIfNeeded(shutdownPendingPacket);
                logError("Viewport native bridge smoke did not preserve pending epoch stats before "
                         "shutdown.");
                return false;
            }
            EditorViewportNativeRuntimeStatsV4 statsV4BeforeShutdown{};
            if (!queryRuntimeStatsV4(statsV4BeforeShutdown) ||
                statsV4BeforeShutdown.rendererCreations != 1U ||
                statsV4BeforeShutdown.packetsCreated != 10U ||
                statsV4BeforeShutdown.frameEpochsSubmitted != 12U ||
                statsV4BeforeShutdown.frameEpochsCompleted != 11U ||
                statsV4BeforeShutdown.frameEpochsPending != 1U ||
                statsV4BeforeShutdown.outstandingPackets != 1U) {
                releaseIfNeeded(shutdownPendingPacket);
                logError("Viewport native bridge smoke did not preserve renderer reuse stats "
                         "before shutdown.");
                return false;
            }

            editor_viewport_shutdown();
            releaseIfNeeded(shutdownPendingPacket);
            if (!waitForRuntimeShutdown()) {
                logError("Viewport native bridge smoke retained an idle shutdown context.");
                return false;
            }

            EditorViewportNativeRuntimeStatsV2 statsAfterRetirement{};
            if (!queryRuntimeStatsV2(statsAfterRetirement) ||
                statsAfterRetirement.hasContext != 0U ||
                statsAfterRetirement.hasRenderProducer != 0U ||
                statsAfterRetirement.shutdownRequested != 1U ||
                statsAfterRetirement.outstandingPackets != 0U) {
                logError("Viewport native bridge smoke reported inconsistent shutdown state.");
                return false;
            }

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

    } // namespace

    bool runViewportNativeBridgeSmoke() {
        const SharedViewportRuntimeShutdown shutdownOnExit;
        return smokeCompatibilityContract() && smokeFirstPacketAndBackpressure() &&
               smokeSameSizeLegacyReuse() && smokeResizeChurn() && smokeReusableSlots() &&
               smokeShutdownOrdering();
    }

} // namespace asharia::editor
