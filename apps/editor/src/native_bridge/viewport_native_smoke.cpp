#include "native_bridge/viewport_native_smoke.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <numbers>
#include <span>
#include <thread>

#include "asharia/core/log.hpp"

#include "editor_shared_viewport_dispatch.hpp"
#include "editor_shared_viewport_runtime.hpp"
#include "native_bridge/viewport_native_api.hpp"

namespace asharia::editor {
    namespace {

        struct ExpectedRenderExtent final {
            std::uint32_t width;
            std::uint32_t height;
        };

        enum class DispatchProbeKind : std::uint8_t {
            Completion,
            Close,
            Render,
        };

        struct DispatchProbeStream final {
            std::uint64_t streamId{};
            std::uint32_t completionSteps{};
            std::uint32_t closeSteps{};
            std::uint32_t renderSteps{};
            bool realtime{};
        };

        struct DispatchProbeEvent final {
            std::uint64_t streamId{};
            DispatchProbeKind kind{DispatchProbeKind::Render};
        };

        [[nodiscard]] EditorViewportNativeId
        nativeIdForCanonicalUuid(std::span<const std::uint8_t, 16> value) noexcept {
            const std::array<std::uint8_t, 16> guidBytes{
                value[3],  value[2],  value[1],  value[0],  value[5],  value[4],
                value[7],  value[6],  value[8],  value[9],  value[10], value[11],
                value[12], value[13], value[14], value[15],
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

        [[nodiscard]] EditorViewportNativePresentRequestV11
        makeFrameRequest(std::uint64_t requestSequence) {
            return EditorViewportNativePresentRequestV11{
                .header =
                    EditorViewportNativeAbiHeader{
                        .abiVersion = EDITOR_NATIVE_ABI_VERSION,
                        .structSize = static_cast<std::uint32_t>(
                            sizeof(EditorViewportNativePresentRequestV11)),
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
                .flags = EditorViewportNativePresentRequestV11Flags_HasLogicalExtent,
                .camera =
                    EditorViewportNativeCamera{
                        .position = {0.0F, 2.0F, -6.0F},
                        .target = {0.0F, 0.0F, 0.0F},
                        .up = {0.0F, 1.0F, 0.0F},
                        .fieldOfViewRadians = 1.57079633F,
                        .fieldOfViewAxis = EditorViewportNativeFieldOfViewAxis_MaintainHorizontal,
                        .nearPlane = 0.1F,
                        .farPlane = 1000.0F,
                    },
                .logicalWidthPixels = 377U,
                .logicalHeightPixels = 219U,
                .authoredMeshes = nullptr,
                .authoredMeshCount = 0U,
                .sceneRasterMode = EditorViewportNativeSceneRasterMode_Solid,
                .selectedObjectId = {},
                .viewStateRevision = 0U,
                .transformGizmo = {},
            };
        }

        [[nodiscard]] bool waitForReadyFrame(std::uint64_t streamId,
                                             EditorViewportNativeReadyFrameV11& frame) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                EditorViewportNativeStreamPollV11 before{};
                if (editor_viewport_poll_stream_v11(streamId, &before) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (editor_viewport_try_take_ready_v11(streamId, &frame) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (frame.hasFrame != 0U) {
                    return true;
                }
                if (editor_viewport_wait_stream_change_v11(streamId, before.stateRevision, 50U) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            return false;
        }

        [[nodiscard]] bool waitForPoll(std::uint64_t streamId,
                                       EditorViewportNativeStreamPollV11& poll,
                                       std::uint32_t lifecycle) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                if (editor_viewport_poll_stream_v11(streamId, &poll) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (poll.lifecycle == lifecycle) {
                    return true;
                }
                if (editor_viewport_wait_stream_change_v11(streamId, poll.stateRevision, 50U) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            return false;
        }

        [[nodiscard]] bool waitForReadyPoll(std::uint64_t streamId,
                                            EditorViewportNativeStreamPollV11& poll) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                if (editor_viewport_poll_stream_v11(streamId, &poll) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (poll.hasReadyFrame != 0U) {
                    return true;
                }
                if (editor_viewport_wait_stream_change_v11(streamId, poll.stateRevision, 50U) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            return false;
        }

        [[nodiscard]] bool smokeStreamNotificationLifetime() {
            const auto compatibility = makeCompatibilityRequest();
            EditorViewportNativeStreamHandleV11 opened{};
            EditorViewportNativeStreamPollV11 before{};
            if (editor_viewport_open_stream_v11(&compatibility, &opened) !=
                    EditorViewportNativeStatus_Success ||
                editor_viewport_poll_stream_v11(opened.streamId, &before) !=
                    EditorViewportNativeStatus_Success) {
                return false;
            }
            if (editor_viewport_wait_stream_change_v11(opened.streamId, 0U, 51U) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                editor_viewport_wait_stream_change_v11(0U, 0U, 0U) !=
                    EditorViewportNativeStatus_InvalidArgument) {
                return false;
            }
            auto waiting = std::async(std::launch::async, [&] {
                return editor_viewport_wait_stream_change_v11(opened.streamId, before.stateRevision,
                                                              50U);
            });
            if (editor_viewport_close_stream_v11(opened.streamId) !=
                    EditorViewportNativeStatus_Success ||
                editor_viewport_close_stream_v11(opened.streamId) !=
                    EditorViewportNativeStatus_Success ||
                waiting.get() != EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeStreamPollV11 closed{};
            if (!waitForPoll(opened.streamId, closed, EditorViewportNativeStreamLifecycle_Closed) ||
                closed.stateRevision <= before.stateRevision ||
                editor_viewport_wait_stream_change_v11(opened.streamId, before.stateRevision,
                                                       50U) != EditorViewportNativeStatus_Success ||
                editor_viewport_destroy_stream_v11(opened.streamId) !=
                    EditorViewportNativeStatus_Success) {
                return false;
            }
            return editor_viewport_wait_stream_change_v11(opened.streamId, closed.stateRevision,
                                                          0U) ==
                   EditorViewportNativeStatus_Unavailable;
        }

        [[nodiscard]] bool waitForNoOutstandingPackets() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                EditorViewportNativeRuntimeStatsV10 stats{};
                if (editor_viewport_query_runtime_stats_v10(&stats) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (stats.outstandingPackets == 0U) {
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

        [[nodiscard]] bool smokeMonotonicFrameClock() {
            using FrameClock = EditorSharedViewportFrameClock;
            const FrameClock::Clock::time_point epoch{};
            FrameClock clock{epoch};

            const BasicRenderViewFrameParams first = clock.frameParams(1U, epoch);
            if (first.frameIndex != 1U || first.timeSeconds != 0.0F || first.deltaSeconds != 0.0F) {
                logError("Viewport frame clock did not start at a zero monotonic epoch.");
                return false;
            }
            clock.markRendered(epoch);

            const auto fiveMilliseconds = epoch + std::chrono::milliseconds{5};
            const BasicRenderViewFrameParams second = clock.frameParams(2U, fiveMilliseconds);
            if (std::abs(second.timeSeconds - 0.005F) > 0.0001F ||
                std::abs(second.deltaSeconds - 0.005F) > 0.0001F) {
                logError("Viewport frame clock used a nominal FPS instead of elapsed time.");
                return false;
            }
            clock.markRendered(fiveMilliseconds);

            const auto failedAttemptAt = epoch + std::chrono::milliseconds{10};
            const BasicRenderViewFrameParams failedAttempt = clock.frameParams(3U, failedAttemptAt);
            if (failedAttempt.frameIndex != 3U ||
                std::abs(failedAttempt.deltaSeconds - 0.005F) > 0.0001F) {
                logError("Viewport frame clock did not identify a render attempt.");
                return false;
            }

            const auto succeededAfterFailureAt = epoch + std::chrono::milliseconds{21};
            const BasicRenderViewFrameParams succeededAfterFailure =
                clock.frameParams(4U, succeededAfterFailureAt);
            if (succeededAfterFailure.frameIndex != 4U ||
                std::abs(succeededAfterFailure.timeSeconds - 0.021F) > 0.0001F ||
                std::abs(succeededAfterFailure.deltaSeconds - 0.016F) > 0.0001F) {
                logError("Viewport frame clock advanced its success sample after a failed render.");
                return false;
            }
            clock.markRendered(succeededAfterFailureAt);

            const auto afterIdle = epoch + std::chrono::milliseconds{2021};
            const BasicRenderViewFrameParams resumed = clock.frameParams(5U, afterIdle);
            if (resumed.frameIndex != 5U || std::abs(resumed.timeSeconds - 2.021F) > 0.0001F ||
                std::abs(resumed.deltaSeconds - 2.0F) > 0.0001F) {
                logError("Viewport frame clock did not preserve elapsed time across idle.");
                return false;
            }

            const auto resetEpoch = epoch + std::chrono::seconds{3};
            clock.reset(resetEpoch);
            const BasicRenderViewFrameParams resetFirst = clock.frameParams(6U, resetEpoch);
            if (resetFirst.frameIndex != 6U || resetFirst.timeSeconds != 0.0F ||
                resetFirst.deltaSeconds != 0.0F) {
                logError("Viewport frame clock reset retained its previous epoch or delta.");
                return false;
            }
            clock.markRendered(resetEpoch);

            const auto afterReset = resetEpoch + std::chrono::milliseconds{7};
            const BasicRenderViewFrameParams resetSecond = clock.frameParams(7U, afterReset);
            if (std::abs(resetSecond.timeSeconds - 0.007F) > 0.0001F ||
                std::abs(resetSecond.deltaSeconds - 0.007F) > 0.0001F) {
                logError("Viewport frame clock reset did not establish a fresh monotonic epoch.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool smokeStableRoundRobinDispatchPolicy() {
            std::array<DispatchProbeStream, 4> streams{
                DispatchProbeStream{.streamId = 10U, .realtime = true},
                DispatchProbeStream{.streamId = 20U, .renderSteps = 1U},
                DispatchProbeStream{.streamId = 30U, .renderSteps = 1U},
                DispatchProbeStream{.streamId = 40U, .renderSteps = 1U},
            };
            std::array<DispatchProbeEvent, 8> events{};
            std::size_t eventCount{};
            std::uint64_t cursor{};
            const auto streamId = [](const DispatchProbeStream& stream) { return stream.streamId; };
            const auto noPriorityWork = [](DispatchProbeStream&) { return false; };
            const auto render = [&events, &eventCount](DispatchProbeStream& stream) {
                if (stream.renderSteps == 0U && !stream.realtime) {
                    return false;
                }
                events.at(eventCount++) = DispatchProbeEvent{
                    .streamId = stream.streamId,
                    .kind = DispatchProbeKind::Render,
                };
                if (stream.renderSteps != 0U) {
                    --stream.renderSteps;
                }
                return true;
            };
            for (std::size_t dispatch = 0U; dispatch < streams.size(); ++dispatch) {
                if (!detail::dispatchOneStableRoundRobin(std::span{streams}, cursor, streamId,
                                                         noPriorityWork, render)) {
                    logError("Viewport scheduler probe stopped before all four streams ran.");
                    return false;
                }
            }
            if (events.at(0).streamId != 10U || events.at(1).streamId != 20U ||
                events.at(2).streamId != 30U || events.at(3).streamId != 40U) {
                logError("Viewport scheduler probe let a realtime stream monopolize dispatch.");
                return false;
            }

            streams = {
                DispatchProbeStream{.streamId = 10U, .renderSteps = 1U},
                DispatchProbeStream{.streamId = 20U, .renderSteps = 1U},
                DispatchProbeStream{.streamId = 30U, .closeSteps = 2U},
                DispatchProbeStream{.streamId = 40U, .completionSteps = 1U},
            };
            eventCount = 0U;
            cursor = 10U;
            const auto priority = [&events, &eventCount](DispatchProbeStream& stream) {
                DispatchProbeKind kind{};
                if (stream.completionSteps != 0U) {
                    --stream.completionSteps;
                    kind = DispatchProbeKind::Completion;
                } else if (stream.closeSteps != 0U) {
                    --stream.closeSteps;
                    kind = DispatchProbeKind::Close;
                } else {
                    return false;
                }
                events.at(eventCount++) =
                    DispatchProbeEvent{.streamId = stream.streamId, .kind = kind};
                return true;
            };
            for (std::size_t dispatch = 0U; dispatch < 4U; ++dispatch) {
                if (!detail::dispatchOneStableRoundRobin(std::span{streams}, cursor, streamId,
                                                         priority, render)) {
                    logError("Viewport scheduler priority probe stopped unexpectedly.");
                    return false;
                }
            }
            if (events.at(0).kind != DispatchProbeKind::Close || events.at(0).streamId != 30U ||
                events.at(1).kind != DispatchProbeKind::Completion ||
                events.at(1).streamId != 40U || events.at(2).kind != DispatchProbeKind::Close ||
                events.at(2).streamId != 30U || events.at(3).kind != DispatchProbeKind::Render) {
                logError(
                    "Viewport scheduler rendered before global completion/close work drained.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool completeNotSubmitted(std::uint64_t streamId, void* nativeSlot) {
            return editor_viewport_complete_frame_v11(
                       streamId, nativeSlot,
                       EditorViewportNativePresentCompletionKind_NotSubmittedToConsumer) ==
                   EditorViewportNativeStatus_Success;
        }

        [[nodiscard]] bool completeConsumerAccessed(std::uint64_t streamId, void* nativeSlot) {
            return editor_viewport_complete_frame_v11(
                       streamId, nativeSlot,
                       EditorViewportNativePresentCompletionKind_ConsumerAccessed) ==
                   EditorViewportNativeStatus_Success;
        }

#if defined(ASHARIA_EDITOR_NATIVE_TESTING)
        [[nodiscard]] bool smokeUnsupportedWireframeRecovery() {
            EditorViewportNativeCompatibilityRequest compatibility = makeCompatibilityRequest();
            EditorViewportNativeStreamHandleV11 stream{};
            if (editor_viewport_open_stream_v11_for_test(
                    &compatibility, EditorViewportNativeStreamCapabilitiesV11_None, &stream) !=
                    EditorViewportNativeStatus_Success ||
                stream.status != EditorViewportNativeStatus_Success || stream.streamId == 0U ||
                stream.capabilities != EditorViewportNativeStreamCapabilitiesV11_None) {
                logError("Viewport V11 unsupported-wireframe smoke could not open a stream.");
                return false;
            }
            const std::uint64_t streamId = stream.streamId;

            EditorViewportNativePresentRequestV11 wireframeRequest = makeFrameRequest(901U);
            wireframeRequest.sceneRasterMode = EditorViewportNativeSceneRasterMode_Wireframe;
            if (editor_viewport_submit_latest_v11(streamId, &wireframeRequest) !=
                EditorViewportNativeStatus_FeatureUnavailable) {
                logError("Viewport V11 did not reject unsupported wireframe precisely.");
                return false;
            }

            EditorViewportNativeStreamPollV11 rejectedPoll{};
            if (editor_viewport_poll_stream_v11(streamId, &rejectedPoll) !=
                    EditorViewportNativeStatus_Success ||
                rejectedPoll.lifecycle != EditorViewportNativeStreamLifecycle_Open ||
                rejectedPoll.hasPendingLatest != 0U || rejectedPoll.hasReadyFrame != 0U ||
                rejectedPoll.renderExecuting != 0U || rejectedPoll.submittedRequests != 0U ||
                rejectedPoll.coalescedRequests != 0U || rejectedPoll.renderedFrames != 0U) {
                logError("Unsupported wireframe mutated or faulted the V11 stream.");
                return false;
            }

            EditorViewportNativePresentRequestV11 solidRequest = makeFrameRequest(902U);
            if (editor_viewport_submit_latest_v11(streamId, &solidRequest) !=
                EditorViewportNativeStatus_Success) {
                logError("Viewport V11 did not recover from wireframe rejection with Solid.");
                return false;
            }
            EditorViewportNativeReadyFrameV11 solidFrame{};
            if (!waitForReadyFrame(streamId, solidFrame) ||
                solidFrame.requestSequence != solidRequest.requestSequence ||
                !completeNotSubmitted(streamId, solidFrame.nativeSlot) ||
                editor_viewport_release_slot_import_v11(streamId, solidFrame.nativeSlot) !=
                    EditorViewportNativeStatus_Success ||
                editor_viewport_close_stream_v11(streamId) != EditorViewportNativeStatus_Success) {
                logError("Viewport V11 Solid recovery did not complete cleanly.");
                return false;
            }
            EditorViewportNativeStreamPollV11 closedPoll{};
            if (!waitForPoll(streamId, closedPoll, EditorViewportNativeStreamLifecycle_Closed) ||
                closedPoll.submittedRequests != 1U || closedPoll.renderedFrames != 1U ||
                editor_viewport_destroy_stream_v11(streamId) !=
                    EditorViewportNativeStatus_Success) {
                logError("Viewport V11 Solid recovery stream did not close cleanly.");
                return false;
            }
            return true;
        }
#endif

        [[nodiscard]] bool waitForLogicalRenderExtent(std::uint64_t requestSequence,
                                                      ExpectedRenderExtent expectedExtent) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                EditorViewportNativeRuntimeStatsV10 stats{};
                if (editor_viewport_query_runtime_stats_v10(&stats) !=
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

        [[nodiscard]] bool waitForGizmoRenderEvidence(std::uint64_t requestSequence) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < deadline) {
                EditorViewportNativeRuntimeStatsV10 stats{};
                if (editor_viewport_query_runtime_stats_v10(&stats) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (stats.lastRequestSequence >= requestSequence) {
                    // Base origin axes plus three shafts and three 12-edge scale cubes.
                    // The selected debug proxy's ordinary axes are deliberately suppressed.
                    return stats.lastDebugProxyCount == 1U && stats.lastDebugWorldLineCount == 42U;
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
            EditorViewportNativeStreamHandleV11 stream{};
            if (editor_viewport_open_stream_v11(&compatibility, &stream) !=
                    EditorViewportNativeStatus_Success ||
                stream.status != EditorViewportNativeStatus_Success || stream.streamId == 0U) {
                logError("Viewport V11 smoke could not open a stream.");
                return false;
            }

            EditorViewportNativePresentRequestV11 firstRequest = makeFrameRequest(1U);
            EditorViewportNativePresentRequestV11 invalidAxisRequest = firstRequest;
            invalidAxisRequest.camera.fieldOfViewAxis = 2U;
            EditorViewportNativePresentRequestV11 zeroFieldOfViewRequest = firstRequest;
            zeroFieldOfViewRequest.camera.fieldOfViewRadians = 0.0F;
            EditorViewportNativePresentRequestV11 excessiveFieldOfViewRequest = firstRequest;
            excessiveFieldOfViewRequest.camera.fieldOfViewRadians = std::numbers::pi_v<float>;
            if (editor_viewport_submit_latest_v11(stream.streamId, &invalidAxisRequest) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                editor_viewport_submit_latest_v11(stream.streamId, &zeroFieldOfViewRequest) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                editor_viewport_submit_latest_v11(stream.streamId, &excessiveFieldOfViewRequest) !=
                    EditorViewportNativeStatus_InvalidArgument) {
                logError("Viewport V11 smoke accepted an invalid camera projection contract.");
                return false;
            }
            const std::array<EditorViewportNativeAuthoredMeshSnapshotV11, 1> authoredMeshes{
                EditorViewportNativeAuthoredMeshSnapshotV11{
                    .objectId = {0x4aU, 0x1fU, 0x9bU, 0x72U, 0x10U, 0x52U, 0x4bU, 0x6cU, 0x83U,
                                 0x5dU, 0x38U, 0x86U, 0x9dU, 0x24U, 0x7fU, 0x4eU},
                    .runtimeEntityIndex = 9U,
                    .runtimeEntityGeneration = 2U,
                    .assetId = {0x7cU, 0x9fU, 0xe8U, 0xacU, 0x3cU, 0x8bU, 0x4fU, 0x66U, 0x96U,
                                0x65U, 0x0aU, 0xf0U, 0xfdU, 0x7bU, 0x69U, 0x3eU},
                    .expectedMeshType = 0x900405520f80e8e6ULL,
                    .position = {0.75F, 0.5F, 2.0F},
                    .rotation = {0.0F, std::sqrt(0.5F), 0.0F, std::sqrt(0.5F)},
                    .scale = {2.0F, 0.75F, 1.5F},
                },
            };
            firstRequest.authoredMeshes = authoredMeshes.data();
            firstRequest.authoredMeshCount = static_cast<std::uint32_t>(authoredMeshes.size());
            const std::array<EditorViewportNativeDebugProxy, 1> debugProxies{
                EditorViewportNativeDebugProxy{
                    .objectId = nativeIdForCanonicalUuid(authoredMeshes.front().objectId),
                    .position = {0.75F, 0.5F, 2.0F},
                    .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
                    .scale = {1.0F, 1.0F, 1.0F},
                },
            };
            firstRequest.debugProxies = debugProxies.data();
            firstRequest.debugProxyCount = static_cast<std::uint32_t>(debugProxies.size());
            firstRequest.flags |=
                EditorViewportNativePresentRequestV11Flags_CaptureSceneMeshEvidence;
            firstRequest.flags |= EditorViewportNativePresentRequestV11Flags_HasSelectionOutline;
            firstRequest.flags |= EditorViewportNativePresentRequestV11Flags_HasTransformGizmo;
            std::ranges::copy(authoredMeshes.front().objectId, firstRequest.selectedObjectId);
            firstRequest.viewStateRevision = 71U;
            firstRequest.transformGizmo = EditorViewportNativeTransformGizmoV11{
                .objectId = debugProxies.front().objectId,
                .position = {0.75F, 0.5F, 2.0F},
                .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
                .kind = EditorViewportNativeTransformGizmoKind_Scale,
                .hoveredAxis = EditorViewportNativeGizmoAxis_X,
                .activeAxis = EditorViewportNativeGizmoAxis_X,
            };

            EditorViewportNativePresentRequestV11 invalidGizmoAxisRequest = firstRequest;
            invalidGizmoAxisRequest.transformGizmo.activeAxis = 4U;
            EditorViewportNativePresentRequestV11 invalidGizmoKindRequest = firstRequest;
            invalidGizmoKindRequest.transformGizmo.kind = 3U;
            EditorViewportNativePresentRequestV11 invalidGizmoRotationRequest = firstRequest;
            invalidGizmoRotationRequest.transformGizmo.rotation[3] = 2.0F;
            EditorViewportNativePresentRequestV11 mismatchedGizmoSelectionRequest = firstRequest;
            mismatchedGizmoSelectionRequest.transformGizmo.objectId.high += 1U;
            EditorViewportNativePresentRequestV11 unflaggedGizmoRequest = firstRequest;
            unflaggedGizmoRequest.flags &=
                ~EditorViewportNativePresentRequestV11Flags_HasTransformGizmo;
            EditorViewportNativePresentRequestV11 orphanGizmoRequest = makeFrameRequest(1U);
            orphanGizmoRequest.flags |=
                EditorViewportNativePresentRequestV11Flags_HasTransformGizmo;
            orphanGizmoRequest.transformGizmo = firstRequest.transformGizmo;
            if (editor_viewport_submit_latest_v11(stream.streamId, &invalidGizmoAxisRequest) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                editor_viewport_submit_latest_v11(stream.streamId,
                                                  &mismatchedGizmoSelectionRequest) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                editor_viewport_submit_latest_v11(stream.streamId, &invalidGizmoKindRequest) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                editor_viewport_submit_latest_v11(stream.streamId, &invalidGizmoRotationRequest) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                editor_viewport_submit_latest_v11(stream.streamId, &unflaggedGizmoRequest) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                editor_viewport_submit_latest_v11(stream.streamId, &orphanGizmoRequest) !=
                    EditorViewportNativeStatus_InvalidArgument) {
                logError("Viewport V11 smoke accepted an invalid Transform Gizmo contract.");
                return false;
            }

            auto duplicateObjectMeshes = std::array{
                authoredMeshes.front(),
                authoredMeshes.front(),
            };
            duplicateObjectMeshes[1].runtimeEntityIndex = 10U;
            EditorViewportNativePresentRequestV11 duplicateObjectRequest = firstRequest;
            duplicateObjectRequest.authoredMeshes = duplicateObjectMeshes.data();
            duplicateObjectRequest.authoredMeshCount =
                static_cast<std::uint32_t>(duplicateObjectMeshes.size());
            if (editor_viewport_submit_latest_v11(stream.streamId, &duplicateObjectRequest) !=
                EditorViewportNativeStatus_InvalidArgument) {
                logError("Viewport V11 smoke accepted duplicate scene object mesh identities.");
                return false;
            }

            auto duplicateEntityMeshes = duplicateObjectMeshes;
            duplicateEntityMeshes[1].objectId[0] ^= 0x01U;
            duplicateEntityMeshes[1].runtimeEntityIndex =
                duplicateEntityMeshes[0].runtimeEntityIndex;
            EditorViewportNativePresentRequestV11 duplicateEntityRequest = firstRequest;
            duplicateEntityRequest.authoredMeshes = duplicateEntityMeshes.data();
            duplicateEntityRequest.authoredMeshCount =
                static_cast<std::uint32_t>(duplicateEntityMeshes.size());
            if (editor_viewport_submit_latest_v11(stream.streamId, &duplicateEntityRequest) !=
                EditorViewportNativeStatus_InvalidArgument) {
                logError("Viewport V11 smoke accepted duplicate runtime entity mesh identities.");
                return false;
            }

            EditorViewportNativeStreamPollV11 rejectedIdentityPoll{};
            if (editor_viewport_poll_stream_v11(stream.streamId, &rejectedIdentityPoll) !=
                    EditorViewportNativeStatus_Success ||
                rejectedIdentityPoll.submittedRequests != 0U ||
                rejectedIdentityPoll.coalescedRequests != 0U ||
                rejectedIdentityPoll.hasPendingLatest != 0U ||
                rejectedIdentityPoll.renderExecuting != 0U) {
                logError("Rejected V11 requests mutated the stream state.");
                return false;
            }

            if (editor_viewport_submit_latest_v11(stream.streamId, &firstRequest) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            const auto firstReadyDeadline =
                std::chrono::steady_clock::now() + std::chrono::seconds{5};
            EditorViewportNativeStreamPollV11 firstReadyPoll{};
            while (std::chrono::steady_clock::now() < firstReadyDeadline) {
                if (editor_viewport_poll_stream_v11(stream.streamId, &firstReadyPoll) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
                if (firstReadyPoll.hasReadyFrame != 0U) {
                    break;
                }
                std::this_thread::yield();
            }
            if (firstReadyPoll.hasReadyFrame == 0U) {
                logError("Viewport V11 smoke did not render its first ready frame.");
                return false;
            }

            // A ready frame blocks another ready publication. Every submit in
            // this burst therefore targets the single pending-latest cell.
            for (std::uint64_t sequence = 2U; sequence <= 32U; ++sequence) {
                EditorViewportNativePresentRequestV11 request = makeFrameRequest(sequence);
                if (editor_viewport_submit_latest_v11(stream.streamId, &request) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            EditorViewportNativeStreamPollV11 burstPoll{};
            if (editor_viewport_poll_stream_v11(stream.streamId, &burstPoll) !=
                    EditorViewportNativeStatus_Success ||
                burstPoll.hasPendingLatest == 0U || burstPoll.coalescedRequests < 30U) {
                logError("Viewport V11 smoke did not coalesce its pending-latest burst.");
                return false;
            }

            // Hold the ready frame long enough for its producer fence to retire. Taking it must
            // explicitly wake the render owner; there may no longer be a timed GPU-retirement
            // poll or another resize submission to rescue the pending-latest request.
            std::this_thread::sleep_for(std::chrono::milliseconds{100});

            EditorViewportNativeReadyFrameV11 firstFrame{};
            if (!waitForReadyFrame(stream.streamId, firstFrame) ||
                firstFrame.requestSequence != 1U || firstFrame.viewStateRevision != 71U ||
                !waitForGizmoRenderEvidence(1U)) {
                logError("Viewport V11 smoke did not receive its first frame.");
                return false;
            }
            const EditorViewportNativeSceneMeshReceiptV11& receipt = firstFrame.sceneMeshReceipt;
            if (receipt.inputCount != 1U || receipt.resolvedCount != 1U ||
                receipt.rejectedCount != 0U || receipt.evidenceAvailable != 1U ||
                receipt.indexedDrawCount != 1U ||
                receipt.rasterMode != EditorViewportNativeSceneRasterMode_Solid ||
                receipt.representativeSourceEntityIndex != 9U ||
                receipt.representativeSourceEntityGeneration != 2U ||
                receipt.meshResourceKey != 0x0EB29D6DE539D278ULL ||
                receipt.materialResourceKey != 0x4153484D41544C01ULL ||
                receipt.productHash != 0x0EB29D6DE539D278ULL || receipt.sceneRevision != 1U ||
                !std::equal(std::begin(receipt.representativeObjectId),
                            std::end(receipt.representativeObjectId),
                            std::begin(authoredMeshes[0].objectId)) ||
                !std::equal(std::begin(receipt.representativeAssetId),
                            std::end(receipt.representativeAssetId),
                            std::begin(authoredMeshes[0].assetId))) {
                logError("Viewport V11 smoke did not publish the authored scene mesh receipt.");
                return false;
            }

            EditorViewportNativeReadyFrameV11 secondFrame{};
            if (!waitForReadyFrame(stream.streamId, secondFrame) ||
                secondFrame.requestSequence != 32U ||
                secondFrame.nativeSlot == firstFrame.nativeSlot ||
                secondFrame.widthPixels != 384U || secondFrame.heightPixels != 224U ||
                secondFrame.logicalWidthPixels != 377U || secondFrame.logicalHeightPixels != 219U ||
                !waitForLogicalRenderExtent(32U,
                                            ExpectedRenderExtent{.width = 377U, .height = 219U})) {
                logError("Viewport V11 smoke did not publish the latest burst request on slot 2.");
                return false;
            }

            EditorViewportNativePresentRequestV11 thirdRequest = makeFrameRequest(33U);
            if (editor_viewport_submit_latest_v11(stream.streamId, &thirdRequest) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeReadyFrameV11 thirdFrame{};
            if (!waitForReadyFrame(stream.streamId, thirdFrame) ||
                thirdFrame.requestSequence != 33U ||
                thirdFrame.nativeSlot == firstFrame.nativeSlot ||
                thirdFrame.nativeSlot == secondFrame.nativeSlot) {
                logError("Viewport V11 smoke did not allocate its third bounded slot.");
                return false;
            }

            EditorViewportNativePresentRequestV11 fourthRequest = makeFrameRequest(34U);
            if (editor_viewport_submit_latest_v11(stream.streamId, &fourthRequest) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeStreamPollV11 boundedPoll{};
            if (editor_viewport_poll_stream_v11(stream.streamId, &boundedPoll) !=
                    EditorViewportNativeStatus_Success ||
                boundedPoll.slotCount != 3U || boundedPoll.hasPendingLatest == 0U ||
                boundedPoll.hasReadyFrame != 0U) {
                logError("Viewport V11 smoke exceeded or bypassed its three-slot bound.");
                return false;
            }

            if (editor_viewport_complete_frame_v11(stream.streamId, firstFrame.nativeSlot, 99U) !=
                    EditorViewportNativeStatus_InvalidArgument ||
                !completeNotSubmitted(stream.streamId, firstFrame.nativeSlot)) {
                logError("Viewport V11 smoke did not preserve ownership after invalid completion.");
                return false;
            }
            EditorViewportNativeStreamPollV11 retainedPoll{};
            if (editor_viewport_poll_stream_v11(stream.streamId, &retainedPoll) !=
                    EditorViewportNativeStatus_Success ||
                retainedPoll.hasPendingLatest == 0U || retainedPoll.hasReadyFrame != 0U) {
                logError("Viewport V11 smoke reused the compositor's sole available slot.");
                return false;
            }
            if (!completeNotSubmitted(stream.streamId, secondFrame.nativeSlot)) {
                return false;
            }
            EditorViewportNativeReadyFrameV11 fourthFrame{};
            if (!waitForReadyFrame(stream.streamId, fourthFrame) ||
                fourthFrame.requestSequence != 34U ||
                fourthFrame.nativeSlot != firstFrame.nativeSlot) {
                logError("Viewport V11 smoke did not reuse the completed persistent slot.");
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
                if (editor_viewport_release_slot_import_v11(stream.streamId, nativeSlot) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            if (editor_viewport_close_stream_v11(stream.streamId) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeStreamPollV11 closedPoll{};
            if (!waitForPoll(stream.streamId, closedPoll,
                             EditorViewportNativeStreamLifecycle_Closed) ||
                closedPoll.slotCount != 3U || closedPoll.submittedRequests != 34U ||
                closedPoll.renderedFrames != 4U ||
                editor_viewport_destroy_stream_v11(stream.streamId) !=
                    EditorViewportNativeStatus_Success) {
                logError("Viewport V11 smoke did not close and destroy its stream.");
                return false;
            }
            return true;
        }

        // The native cap is intentionally still four. This smoke proves only
        // that four cold streams each receive their first slot and that a
        // ready+pending realtime stream cannot consume the other cold slots.
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [[nodiscard]] bool smokeFourStreamColdStartFairness() {
            constexpr std::size_t kStreamCount = 4U;
            constexpr std::uint64_t kRealtimeFirstSequence = 1'000U;
            std::array<EditorViewportNativeStreamHandleV11, kStreamCount> streams{};
            EditorViewportNativeCompatibilityRequest compatibility = makeCompatibilityRequest();
            for (EditorViewportNativeStreamHandleV11& stream : streams) {
                if (editor_viewport_open_stream_v11(&compatibility, &stream) !=
                        EditorViewportNativeStatus_Success ||
                    stream.status != EditorViewportNativeStatus_Success || stream.streamId == 0U) {
                    logError("Viewport fairness smoke could not open four streams.");
                    return false;
                }
            }

            EditorViewportNativePresentRequestV11 first = makeFrameRequest(kRealtimeFirstSequence);
            if (editor_viewport_submit_latest_v11(streams.at(0).streamId, &first) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeStreamPollV11 realtimeReady{};
            if (!waitForReadyPoll(streams.at(0).streamId, realtimeReady)) {
                logError("Viewport fairness smoke did not prepare the realtime stream.");
                return false;
            }

            for (std::uint64_t sequence = kRealtimeFirstSequence + 1U;
                 sequence <= kRealtimeFirstSequence + 8U; ++sequence) {
                EditorViewportNativePresentRequestV11 request = makeFrameRequest(sequence);
                if (editor_viewport_submit_latest_v11(streams.at(0).streamId, &request) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            std::array<std::uint64_t, kStreamCount> expectedSequences{
                kRealtimeFirstSequence,
                2'001U,
                2'002U,
                2'003U,
            };
            for (std::size_t index = 1U; index < streams.size(); ++index) {
                EditorViewportNativePresentRequestV11 request =
                    makeFrameRequest(expectedSequences.at(index));
                request.targetId.low += static_cast<std::uint64_t>(index);
                if (editor_viewport_submit_latest_v11(streams.at(index).streamId, &request) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }

            for (std::size_t index = 1U; index < streams.size(); ++index) {
                EditorViewportNativeStreamPollV11 poll{};
                if (!waitForReadyPoll(streams.at(index).streamId, poll) ||
                    poll.renderedFrames != 1U) {
                    logError("Viewport fairness smoke starved a cold stream first frame.");
                    return false;
                }
            }
            EditorViewportNativeStreamPollV11 realtimePoll{};
            if (editor_viewport_poll_stream_v11(streams.at(0).streamId, &realtimePoll) !=
                    EditorViewportNativeStatus_Success ||
                realtimePoll.renderedFrames != 1U || realtimePoll.hasReadyFrame == 0U ||
                realtimePoll.hasPendingLatest == 0U || realtimePoll.coalescedRequests < 7U) {
                logError("Viewport fairness smoke lost or over-rendered realtime pending work.");
                return false;
            }

            for (std::size_t index = 0U; index < streams.size(); ++index) {
                EditorViewportNativeReadyFrameV11 frame{};
                if (!waitForReadyFrame(streams.at(index).streamId, frame) ||
                    frame.requestSequence != expectedSequences.at(index) ||
                    !completeNotSubmitted(streams.at(index).streamId, frame.nativeSlot) ||
                    editor_viewport_release_slot_import_v11(streams.at(index).streamId,
                                                            frame.nativeSlot) !=
                        EditorViewportNativeStatus_Success) {
                    logError("Viewport fairness smoke could not release a cold stream frame.");
                    return false;
                }
            }
            // Leave the realtime successor pending while the other streams
            // complete and close. Lifecycle work must make progress before
            // the newly freed capacity can be used for another render.
            for (std::size_t index = 1U; index < streams.size(); ++index) {
                if (editor_viewport_close_stream_v11(streams.at(index).streamId) !=
                    EditorViewportNativeStatus_Success) {
                    return false;
                }
            }
            for (std::size_t index = 1U; index < streams.size(); ++index) {
                EditorViewportNativeStreamPollV11 closedPoll{};
                if (!waitForPoll(streams.at(index).streamId, closedPoll,
                                 EditorViewportNativeStreamLifecycle_Closed) ||
                    closedPoll.renderedFrames != 1U ||
                    editor_viewport_destroy_stream_v11(streams.at(index).streamId) !=
                        EditorViewportNativeStatus_Success) {
                    logError("Viewport fairness smoke let pending render work starve close.");
                    return false;
                }
            }
            if (editor_viewport_close_stream_v11(streams.at(0).streamId) !=
                EditorViewportNativeStatus_Success) {
                return false;
            }
            EditorViewportNativeStreamPollV11 realtimeClosedPoll{};
            if (!waitForPoll(streams.at(0).streamId, realtimeClosedPoll,
                             EditorViewportNativeStreamLifecycle_Closed) ||
                realtimeClosedPoll.renderedFrames < 1U || realtimeClosedPoll.renderedFrames > 2U ||
                editor_viewport_destroy_stream_v11(streams.at(0).streamId) !=
                    EditorViewportNativeStatus_Success) {
                logError("Viewport fairness smoke did not close the realtime stream.");
                return false;
            }
            if (!waitForNoOutstandingPackets()) {
                logError("Viewport fairness smoke did not retire all four native packets.");
                return false;
            }
            return true;
        }

    } // namespace

    bool runViewportNativeBridgeSmoke() {
#if defined(ASHARIA_EDITOR_NATIVE_TESTING)
        const bool unsupportedWireframeRecovered = smokeUnsupportedWireframeRecovery();
#else
        constexpr bool unsupportedWireframeRecovered = true;
#endif
        if (!smokeMonotonicFrameClock() || !smokeStableRoundRobinDispatchPolicy() ||
            !smokeStreamNotificationLifetime() || !queryCompatibility() ||
            !unsupportedWireframeRecovered || !smokeBoundedLatestWinsStream() ||
            !smokeFourStreamColdStartFairness()) {
            editor_viewport_shutdown();
            return false;
        }

        editor_viewport_shutdown();
        EditorViewportNativeRenderThreadStats stats{};
        if (editor_viewport_query_render_thread_stats(&stats) !=
                EditorViewportNativeStatus_Success ||
            stats.lifecycle != EditorViewportNativeRuntimeLifecycle_Stopped ||
            stats.renderThreadRunning != 0U || stats.renderThreadJoined == 0U) {
            logError("Viewport V11 smoke did not stop and join its native render thread.");
            return false;
        }
        return true;
    }

} // namespace asharia::editor
