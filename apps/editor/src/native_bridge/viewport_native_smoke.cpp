#include "native_bridge/viewport_native_smoke.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>

#include "asharia/core/log.hpp"

#include "native_bridge/viewport_native_api.hpp"

namespace asharia::editor {
    namespace {

        struct ExpectedRenderExtent final {
            std::uint32_t width;
            std::uint32_t height;
        };

        [[nodiscard]] EditorViewportNativeCompatibilityRequest makeCompatibilityRequest() {
            return EditorViewportNativeCompatibilityRequest{
                .header =
                    EditorViewportNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize = static_cast<std::uint32_t>(
                            sizeof(EditorViewportNativeCompatibilityRequest)),
                    },
                .imageHandleType = EditorViewportNativeHandleType_VulkanOpaqueNt,
                .semaphoreHandleType = EditorViewportNativeHandleType_VulkanOpaqueNt,
                .deviceLuidLowPart = 0U,
                .deviceLuidHighPart = 0,
                .hasDeviceLuid = 0U,
                .deviceUuidLow = 0U,
                .deviceUuidHigh = 0U,
                .hasDeviceUuid = 0U,
            };
        }

        [[nodiscard]] EditorViewportNativePresentRequestV5
        makeFrameRequest(std::uint64_t requestSequence) {
            return EditorViewportNativePresentRequestV5{
                .header =
                    EditorViewportNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize = static_cast<std::uint32_t>(
                            sizeof(EditorViewportNativePresentRequestV5)),
                    },
                .sessionId = EditorViewportNativeId{.low = 11U, .high = 12U},
                .targetId = EditorViewportNativeId{.low = 21U, .high = 22U},
                .targetRevision = requestSequence,
                .requestSequence = requestSequence,
                .debugProxies = nullptr,
                .debugProxyCount = 0U,
                .kind = EditorViewportNativeRenderKind_Scene,
                .targetKind = EditorViewportNativeTargetKind_DocumentScene,
                .widthPixels = 384U,
                .heightPixels = 224U,
                .flags = EditorViewportNativePresentRequestV5Flags_HasLogicalExtent,
                .camera =
                    EditorViewportNativeCamera{
                        .position = {0.0F, 2.0F, -6.0F},
                        .target = {0.0F, 0.0F, 0.0F},
                        .up = {0.0F, 1.0F, 0.0F},
                        .verticalFovRadians = 1.04719758F,
                        .nearPlane = 0.1F,
                        .farPlane = 1000.0F,
                    },
                .logicalWidthPixels = 377U,
                .logicalHeightPixels = 219U,
            };
        }

        [[nodiscard]] bool waitForReadyFrame(std::uint64_t streamId,
                                             EditorViewportNativeReadyFrameV5& frame) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                if (editor_viewport_try_take_ready_v5(streamId, &frame) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (frame.hasFrame != 0U) {
                    return true;
                }
                std::this_thread::yield();
            }
            return false;
        }

        [[nodiscard]] bool waitForPoll(std::uint64_t streamId,
                                       EditorViewportNativeStreamPollV5& poll,
                                       std::uint32_t lifecycle) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                if (editor_viewport_poll_stream_v5(streamId, &poll) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (poll.lifecycle == lifecycle) {
                    return true;
                }
                std::this_thread::yield();
            }
            return false;
        }

        [[nodiscard]] bool queryCompatibility() {
            EditorViewportNativeCompatibilityRequest compatibility = makeCompatibilityRequest();
            EditorViewportNativeCompatibilityResult result{};
            const bool compatible =
                editor_viewport_query_composition_compatibility(&compatibility, &result) ==
                    EditorViewportNativeStatus_Success &&
                result.status == EditorViewportNativeStatus_Success &&
                result.producedImageHandleType == EditorViewportNativeHandleType_VulkanOpaqueNt &&
                result.producedSemaphoreHandleType == EditorViewportNativeHandleType_VulkanOpaqueNt;
            editor_viewport_release_compatibility_result(result);
            return compatible;
        }

        [[nodiscard]] bool completeNotSubmitted(std::uint64_t streamId, void* nativeSlot) {
            return editor_viewport_complete_frame_v5(
                       streamId, nativeSlot,
                       EditorViewportNativePresentCompletionKind_NotSubmittedToConsumer) ==
                   EditorViewportNativeStatus_Success;
        }

        [[nodiscard]] bool completeConsumerAccessed(std::uint64_t streamId, void* nativeSlot) {
            return editor_viewport_complete_frame_v5(
                       streamId, nativeSlot,
                       EditorViewportNativePresentCompletionKind_ConsumerAccessed) ==
                   EditorViewportNativeStatus_Success;
        }

        [[nodiscard]] bool waitForLogicalRenderExtent(std::uint64_t requestSequence,
                                                      ExpectedRenderExtent expectedExtent) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                EditorViewportNativeRuntimeStatsV8 stats{};
                if (editor_viewport_query_runtime_stats_v8(&stats) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (stats.lastRequestSequence >= requestSequence) {
                    return stats.lastRenderWidthPixels == expectedExtent.width &&
                           stats.lastRenderHeightPixels == expectedExtent.height;
                }
                std::this_thread::yield();
            }
            return false;
        }

        // This end-to-end state-machine smoke intentionally keeps the complete
        // submit/take/complete/close timeline visible in one function.
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [[nodiscard]] bool smokeBoundedLatestWinsStream() {
            EditorViewportNativeCompatibilityRequest compatibility = makeCompatibilityRequest();
            EditorViewportNativeStreamHandleV5 stream{};
            if (editor_viewport_open_stream_v5(&compatibility, &stream) !=
                    EditorViewportNativeStatus_Success ||
                stream.status != EditorViewportNativeStatus_Success || stream.streamId == 0U) {
                logError("Viewport V5 smoke could not open a stream.");
                return false;
            }

            EditorViewportNativePresentRequestV5 firstRequest = makeFrameRequest(1U);
            if (editor_viewport_submit_latest_v5(stream.streamId, &firstRequest) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            const auto firstReadyDeadline =
                std::chrono::steady_clock::now() + std::chrono::seconds{5};
            EditorViewportNativeStreamPollV5 firstReadyPoll{};
            while (std::chrono::steady_clock::now() < firstReadyDeadline) {
                if (editor_viewport_poll_stream_v5(stream.streamId, &firstReadyPoll) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (firstReadyPoll.hasReadyFrame != 0U) {
                    break;
                }
                std::this_thread::yield();
            }
            if (firstReadyPoll.hasReadyFrame == 0U) {
                logError("Viewport V5 smoke did not render its first ready frame.");
                return false;
            }

            // A ready frame blocks another ready publication. Every submit in
            // this burst therefore targets the single pending-latest cell.
            for (std::uint64_t sequence = 2U; sequence <= 32U; ++sequence) {
                EditorViewportNativePresentRequestV5 request = makeFrameRequest(sequence);
                if (editor_viewport_submit_latest_v5(stream.streamId, &request) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            EditorViewportNativeStreamPollV5 burstPoll{};
            if (editor_viewport_poll_stream_v5(stream.streamId, &burstPoll) !=
                    EditorViewportNativeStatus_Success ||
                burstPoll.hasPendingLatest == 0U || burstPoll.coalescedRequests < 30U) {
                logError("Viewport V5 smoke did not coalesce its pending-latest burst.");
                return false;
            }

            // Hold the ready frame long enough for its producer fence to retire. Taking it must
            // explicitly wake the render owner; there may no longer be a timed GPU-retirement
            // poll or another resize submission to rescue the pending-latest request.
            std::this_thread::sleep_for(std::chrono::milliseconds{100});

            EditorViewportNativeReadyFrameV5 firstFrame{};
            if (!waitForReadyFrame(stream.streamId, firstFrame) ||
                firstFrame.requestSequence != 1U) {
                logError("Viewport V5 smoke did not receive its first frame.");
                return false;
            }

            EditorViewportNativeReadyFrameV5 secondFrame{};
            if (!waitForReadyFrame(stream.streamId, secondFrame) ||
                secondFrame.requestSequence != 32U ||
                secondFrame.nativeSlot == firstFrame.nativeSlot ||
                secondFrame.widthPixels != 384U || secondFrame.heightPixels != 224U ||
                secondFrame.logicalWidthPixels != 377U || secondFrame.logicalHeightPixels != 219U ||
                !waitForLogicalRenderExtent(32U,
                                            ExpectedRenderExtent{.width = 377U, .height = 219U})) {
                logError("Viewport V5 smoke did not publish the latest burst request on slot 2.");
                return false;
            }

            EditorViewportNativePresentRequestV5 thirdRequest = makeFrameRequest(33U);
            if (editor_viewport_submit_latest_v5(stream.streamId, &thirdRequest) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeReadyFrameV5 thirdFrame{};
            if (!waitForReadyFrame(stream.streamId, thirdFrame) ||
                thirdFrame.requestSequence != 33U ||
                thirdFrame.nativeSlot == firstFrame.nativeSlot ||
                thirdFrame.nativeSlot == secondFrame.nativeSlot) {
                logError("Viewport V5 smoke did not allocate its third bounded slot.");
                return false;
            }

            EditorViewportNativePresentRequestV5 fourthRequest = makeFrameRequest(34U);
            if (editor_viewport_submit_latest_v5(stream.streamId, &fourthRequest) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeStreamPollV5 boundedPoll{};
            if (editor_viewport_poll_stream_v5(stream.streamId, &boundedPoll) !=
                    EditorViewportNativeStatus_Success ||
                boundedPoll.slotCount != 3U || boundedPoll.hasPendingLatest == 0U ||
                boundedPoll.hasReadyFrame != 0U) {
                logError("Viewport V5 smoke exceeded or bypassed its three-slot bound.");
                return false;
            }

            if (editor_viewport_complete_frame_v5(stream.streamId, firstFrame.nativeSlot, 99U) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                !completeNotSubmitted(stream.streamId, firstFrame.nativeSlot)) {
                logError("Viewport V5 smoke did not preserve ownership after invalid completion.");
                return false;
            }
            EditorViewportNativeStreamPollV5 retainedPoll{};
            if (editor_viewport_poll_stream_v5(stream.streamId, &retainedPoll) !=
                    EditorViewportNativeStatus_Success ||
                retainedPoll.hasPendingLatest == 0U || retainedPoll.hasReadyFrame != 0U) {
                logError("Viewport V5 smoke reused the compositor's sole available slot.");
                return false;
            }
            if (!completeNotSubmitted(stream.streamId, secondFrame.nativeSlot)) {
                return false;
            }
            EditorViewportNativeReadyFrameV5 fourthFrame{};
            if (!waitForReadyFrame(stream.streamId, fourthFrame) ||
                fourthFrame.requestSequence != 34U ||
                fourthFrame.nativeSlot != firstFrame.nativeSlot) {
                logError("Viewport V5 smoke did not reuse the completed persistent slot.");
                return false;
            }

            if (!completeNotSubmitted(stream.streamId, thirdFrame.nativeSlot) ||
                !completeConsumerAccessed(stream.streamId, fourthFrame.nativeSlot)) {
                return false;
            }
            const std::array<void*, 3> importedSlots{
                firstFrame.nativeSlot,
                secondFrame.nativeSlot,
                thirdFrame.nativeSlot,
            };
            for (void* nativeSlot : importedSlots) {
                if (editor_viewport_release_slot_import_v5(stream.streamId, nativeSlot) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            if (editor_viewport_close_stream_v5(stream.streamId) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeStreamPollV5 closedPoll{};
            if (!waitForPoll(stream.streamId, closedPoll,
                             EditorViewportNativeStreamLifecycle_Closed) ||
                closedPoll.slotCount != 3U || closedPoll.submittedRequests != 34U ||
                closedPoll.renderedFrames != 4U ||
                editor_viewport_destroy_stream_v5(stream.streamId) !=
                    EditorViewportNativeStatus_Success) {
                logError("Viewport V5 smoke did not close and destroy its stream.");
                return false;
            }
            return true;
        }

    } // namespace

    bool runViewportNativeBridgeSmoke() {
        if (!queryCompatibility() || !smokeBoundedLatestWinsStream()) {
            editor_viewport_shutdown();
            return false;
        }

        editor_viewport_shutdown();
        EditorViewportNativeRenderThreadStats stats{};
        if (editor_viewport_query_render_thread_stats(&stats) !=
                EditorViewportNativeStatus_Success ||
            stats.lifecycle != EditorViewportNativeRuntimeLifecycle_Stopped ||
            stats.renderThreadRunning != 0U || stats.renderThreadJoined == 0U) {
            logError("Viewport V5 smoke did not stop and join its native render thread.");
            return false;
        }
        return true;
    }

} // namespace asharia::editor
