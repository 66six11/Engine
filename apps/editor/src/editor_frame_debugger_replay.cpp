#include "editor_frame_debugger_replay.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "editor_frame_debugger.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] bool
        previewableImageResource(const asharia::RenderGraphDiagnosticsResourceNode& resource) {
            return resource.kind == asharia::RenderGraphResourceKind::Image &&
                   resource.imageFormat == asharia::RenderGraphImageFormat::B8G8R8A8Srgb;
        }

        [[nodiscard]] const asharia::RenderGraphDiagnosticsResourceNode*
        findCapturedImageResource(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                  std::uint32_t resourceIndex) {
            for (const asharia::RenderGraphDiagnosticsResourceNode& resource : snapshot.resources) {
                if (resource.kind == asharia::RenderGraphResourceKind::Image &&
                    resource.resourceIndex == resourceIndex) {
                    return &resource;
                }
            }
            return nullptr;
        }

        [[nodiscard]] const asharia::BasicRenderViewExecutionEvent*
        findCapturedExecutionEvent(const asharia::BasicRenderViewDiagnostics& diagnostics,
                                   asharia::BasicRenderViewExecutionEventId eventId) {
            for (const asharia::BasicRenderViewExecutionEvent& event :
                 diagnostics.executionEvents) {
                if (event.id == eventId) {
                    return &event;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool passExists(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                      std::size_t passIndex) {
            return std::ranges::any_of(
                snapshot.passes, [passIndex](const asharia::RenderGraphDiagnosticsPassNode& pass) {
                    return pass.passIndex == passIndex;
                });
        }

        [[nodiscard]] bool replayOutputAccess(asharia::RenderGraphSlotAccess access,
                                              asharia::RenderGraphSlotAccess preferredAccess) {
            return access == preferredAccess &&
                   (access == asharia::RenderGraphSlotAccess::ColorWrite ||
                    access == asharia::RenderGraphSlotAccess::TransferWrite);
        }

        [[nodiscard]] std::optional<std::uint32_t>
        previewableImageOutputForPass(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                      std::size_t passIndex, std::string& message) {
            if (!passExists(snapshot, passIndex)) {
                message = "Selected pass was not captured.";
                return std::nullopt;
            }

            bool hasImageOutput = false;
            constexpr std::array kPreferredOutputAccess{
                asharia::RenderGraphSlotAccess::ColorWrite,
                asharia::RenderGraphSlotAccess::TransferWrite,
            };
            for (const asharia::RenderGraphSlotAccess preferredAccess : kPreferredOutputAccess) {
                for (const asharia::RenderGraphDiagnosticsAccessEdge& edge : snapshot.accessEdges) {
                    if (edge.passIndex != passIndex ||
                        edge.resourceKind != asharia::RenderGraphResourceKind::Image ||
                        !replayOutputAccess(edge.access, preferredAccess)) {
                        continue;
                    }

                    hasImageOutput = true;
                    const asharia::RenderGraphDiagnosticsResourceNode* resource =
                        findCapturedImageResource(snapshot, edge.resourceIndex);
                    if (resource != nullptr && previewableImageResource(*resource)) {
                        return edge.resourceIndex;
                    }
                }
            }

            message = hasImageOutput ? "Selected pass output is not previewable."
                                     : "Selected pass has no image output.";
            return std::nullopt;
        }

        [[nodiscard]] const EditorFrameDebugCapture*
        activeCapture(const std::optional<EditorFrameDebugCapture>& pausedCapture,
                      const std::optional<EditorFrameDebugCapture>& latestCapture) {
            if (pausedCapture) {
                return &(*pausedCapture);
            }
            if (latestCapture) {
                return &(*latestCapture);
            }
            return nullptr;
        }

    } // namespace

    namespace frame_debugger_replay {

        std::optional<std::size_t>
        defaultReplayPassIndex(const asharia::RenderGraphDiagnosticsSnapshot& snapshot) {
            std::optional<std::size_t> selectedPass;
            for (const asharia::RenderGraphDiagnosticsPassNode& pass : snapshot.passes) {
                std::string message;
                if (previewableImageOutputForPass(snapshot, pass.passIndex, message)) {
                    selectedPass = pass.passIndex;
                }
            }
            return selectedPass;
        }

    } // namespace frame_debugger_replay

    bool EditorFrameDebugger::selectReplayPass(std::size_t passIndex) {
        const EditorFrameDebugCapture* capture = activeCapture(pausedCapture_, latestCapture_);
        if (capture == nullptr) {
            return false;
        }

        const bool changed =
            !preview_.selectedPassIndex || *preview_.selectedPassIndex != passIndex;
        preview_.selectedPassIndex = passIndex;
        preview_.selectedExecutionEventId.reset();
        preview_.selectedImageResourceIndex.reset();
        preview_.texture = {};
        preview_.message.clear();
        ++stats_.replayPassRequests;

        std::string message;
        const std::optional<std::uint32_t> previewImage =
            previewableImageOutputForPass(capture->diagnostics.renderGraph, passIndex, message);
        if (!previewImage) {
            preview_.status = EditorFrameDebugPreviewStatus::Unavailable;
            preview_.message = std::move(message);
            preview_.dirty = false;
            ++stats_.replayPassUnavailableRequests;
            if (changed) {
                ++stats_.replayPassSelections;
            }
            return true;
        }

        preview_.selectedImageResourceIndex = *previewImage;
        preview_.status = EditorFrameDebugPreviewStatus::Pending;
        preview_.dirty = true;
        ++stats_.previewRequests;
        if (changed) {
            ++stats_.replayPassSelections;
        }
        return true;
    }

    bool EditorFrameDebugger::selectReplayEvent(asharia::BasicRenderViewExecutionEventId eventId) {
        const EditorFrameDebugCapture* capture = activeCapture(pausedCapture_, latestCapture_);
        if (capture == nullptr) {
            return false;
        }

        const asharia::BasicRenderViewExecutionEvent* event =
            findCapturedExecutionEvent(capture->diagnostics, eventId);
        if (event == nullptr) {
            return false;
        }

        const bool changed =
            !preview_.selectedExecutionEventId || *preview_.selectedExecutionEventId != eventId;
        preview_.selectedExecutionEventId = eventId;
        preview_.selectedPassIndex = event->passIndex;
        preview_.selectedImageResourceIndex.reset();
        preview_.texture = {};
        preview_.message.clear();
        ++stats_.replayEventRequests;

        std::string message;
        const std::optional<std::uint32_t> previewImage = previewableImageOutputForPass(
            capture->diagnostics.renderGraph, event->passIndex, message);
        if (!previewImage) {
            preview_.status = EditorFrameDebugPreviewStatus::Unavailable;
            preview_.message = std::move(message);
            preview_.dirty = false;
            ++stats_.replayPassUnavailableRequests;
            if (changed) {
                ++stats_.replayEventSelections;
            }
            return true;
        }

        preview_.selectedImageResourceIndex = *previewImage;
        preview_.status = EditorFrameDebugPreviewStatus::Pending;
        preview_.dirty = true;
        ++stats_.previewRequests;
        if (changed) {
            ++stats_.replayEventSelections;
        }
        return true;
    }

    bool EditorFrameDebugger::selectPreviewImageResource(std::uint32_t resourceIndex) {
        if (!pausedCapture_ && !latestCapture_) {
            return false;
        }

        const bool changed = !preview_.selectedImageResourceIndex ||
                             *preview_.selectedImageResourceIndex != resourceIndex;
        preview_.selectedPassIndex.reset();
        preview_.selectedExecutionEventId.reset();
        preview_.selectedImageResourceIndex = resourceIndex;
        preview_.texture = {};
        preview_.message.clear();
        preview_.status = EditorFrameDebugPreviewStatus::Pending;
        preview_.dirty = true;
        ++stats_.previewRequests;
        if (changed) {
            ++stats_.previewSelections;
        }
        return true;
    }

    std::optional<std::uint32_t> EditorFrameDebugger::consumePreviewRequest() {
        if (state_ != EditorFrameDebuggerState::PausedFrameDebug || !preview_.dirty ||
            !preview_.selectedImageResourceIndex) {
            return std::nullopt;
        }

        preview_.dirty = false;
        return preview_.selectedImageResourceIndex;
    }

    void EditorFrameDebugger::publishPreviewTexture(std::uint32_t resourceIndex,
                                                    EditorViewportTexture texture) {
        preview_.selectedImageResourceIndex = resourceIndex;
        preview_.texture = texture;
        preview_.message.clear();
        preview_.status = EditorFrameDebugPreviewStatus::Available;
        preview_.dirty = false;
        ++stats_.previewFramesRecorded;
        ++stats_.previewTextureFramesPublished;
    }

    void EditorFrameDebugger::markPreviewUnavailable(std::uint32_t resourceIndex,
                                                     std::string message) {
        preview_.selectedImageResourceIndex = resourceIndex;
        preview_.texture = {};
        preview_.message = std::move(message);
        preview_.status = EditorFrameDebugPreviewStatus::Unavailable;
        preview_.dirty = false;
        ++stats_.previewFramesRecorded;
        ++stats_.previewUnavailableFrames;
    }

} // namespace asharia::editor
