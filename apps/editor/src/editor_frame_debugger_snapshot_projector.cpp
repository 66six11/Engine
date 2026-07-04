#include "editor_frame_debugger_snapshot_projector.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/renderer_basic_vulkan/render_view.hpp"
#include "asharia/rendergraph/render_graph_diagnostics.hpp"
#include "asharia/rendergraph/render_graph_types.hpp"

#include "editor_frame_debugger.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] archive::ArchiveMember member(std::string key,
                                                    archive::ArchiveValue value) {
            return archive::ArchiveMember{.key = std::move(key), .value = std::move(value)};
        }

        [[nodiscard]] archive::ArchiveValue stringValue(std::string_view value) {
            return archive::ArchiveValue::string(std::string{value});
        }

        [[nodiscard]] archive::ArchiveValue integerValue(std::int64_t value) {
            return archive::ArchiveValue::integer(value);
        }

        [[nodiscard]] archive::ArchiveValue countValue(std::size_t value) {
            return integerValue(static_cast<std::int64_t>(value));
        }

        [[nodiscard]] archive::ArchiveValue uint32Value(std::uint32_t value) {
            return integerValue(static_cast<std::int64_t>(value));
        }

        [[nodiscard]] archive::ArchiveValue uint64Value(std::uint64_t value) {
            return integerValue(static_cast<std::int64_t>(value));
        }

        [[nodiscard]] archive::ArchiveValue boolValue(bool value) {
            return archive::ArchiveValue::boolean(value);
        }

        [[nodiscard]] archive::ArchiveValue optionalSizeValue(std::optional<std::size_t> value) {
            if (!value) {
                return archive::ArchiveValue::null();
            }
            return countValue(*value);
        }

        [[nodiscard]] archive::ArchiveValue optionalUint32Value(
            std::optional<std::uint32_t> value) {
            if (!value) {
                return archive::ArchiveValue::null();
            }
            return uint32Value(*value);
        }

        [[nodiscard]] std::string passId(std::size_t passIndex) {
            return "pass:" + std::to_string(passIndex);
        }

        [[nodiscard]] std::string commandId(std::size_t passIndex, std::size_t commandIndex) {
            return "command:" + std::to_string(passIndex) + ":" + std::to_string(commandIndex);
        }

        [[nodiscard]] std::string resourceId(RenderGraphResourceKind kind,
                                             std::uint32_t resourceIndex) {
            const std::string_view prefix =
                kind == RenderGraphResourceKind::Buffer ? "buffer:" : "image:";
            return std::string{prefix} + std::to_string(resourceIndex);
        }

        [[nodiscard]] std::string eventId(BasicRenderViewExecutionEventId executionEventId) {
            return "event:" + std::to_string(executionEventId.value);
        }

        [[nodiscard]] std::string_view viewportKindName(EditorViewportKind kind) {
            switch (kind) {
            case EditorViewportKind::Scene:
                return "Scene";
            case EditorViewportKind::Game:
                return "Game";
            case EditorViewportKind::Preview:
                return "Preview";
            }
            return "Scene";
        }

        [[nodiscard]] std::string_view resourceKindName(RenderGraphResourceKind kind) {
            switch (kind) {
            case RenderGraphResourceKind::Image:
                return "Image";
            case RenderGraphResourceKind::Buffer:
                return "Buffer";
            }
            return "Image";
        }

        [[nodiscard]] std::string_view imageFormatName(RenderGraphImageFormat format) {
            switch (format) {
            case RenderGraphImageFormat::B8G8R8A8Srgb:
                return "B8G8R8A8Srgb";
            case RenderGraphImageFormat::D32Sfloat:
                return "D32Sfloat";
            case RenderGraphImageFormat::Undefined:
            default:
                return "Undefined";
            }
        }

        [[nodiscard]] std::string_view imageStateName(RenderGraphImageState state) {
            switch (state) {
            case RenderGraphImageState::ColorAttachment:
                return "ColorAttachment";
            case RenderGraphImageState::ShaderRead:
                return "ShaderRead";
            case RenderGraphImageState::DepthAttachmentRead:
                return "DepthAttachmentRead";
            case RenderGraphImageState::DepthAttachmentWrite:
                return "DepthAttachmentWrite";
            case RenderGraphImageState::DepthSampledRead:
                return "DepthSampledRead";
            case RenderGraphImageState::TransferSrc:
                return "TransferSrc";
            case RenderGraphImageState::TransferDst:
                return "TransferDst";
            case RenderGraphImageState::Present:
                return "Present";
            case RenderGraphImageState::Undefined:
            default:
                return "Undefined";
            }
        }

        [[nodiscard]] std::string_view bufferStateName(RenderGraphBufferState state) {
            switch (state) {
            case RenderGraphBufferState::TransferRead:
                return "TransferRead";
            case RenderGraphBufferState::TransferWrite:
                return "TransferWrite";
            case RenderGraphBufferState::HostRead:
                return "HostRead";
            case RenderGraphBufferState::ShaderRead:
                return "ShaderRead";
            case RenderGraphBufferState::StorageReadWrite:
                return "StorageReadWrite";
            case RenderGraphBufferState::Undefined:
            default:
                return "Undefined";
            }
        }

        [[nodiscard]] std::string_view shaderStageName(RenderGraphShaderStage stage) {
            switch (stage) {
            case RenderGraphShaderStage::Fragment:
                return "fragment";
            case RenderGraphShaderStage::Compute:
                return "compute";
            case RenderGraphShaderStage::None:
            default:
                return "";
            }
        }

        [[nodiscard]] std::string imageAccessName(RenderGraphImageAccess access) {
            std::string name{imageStateName(access.state)};
            if ((access.state == RenderGraphImageState::ShaderRead ||
                 access.state == RenderGraphImageState::DepthSampledRead) &&
                access.shaderStage != RenderGraphShaderStage::None) {
                name += "(";
                name += shaderStageName(access.shaderStage);
                name += ")";
            }
            return name;
        }

        [[nodiscard]] std::string bufferAccessName(RenderGraphBufferAccess access) {
            std::string name{bufferStateName(access.state)};
            if ((access.state == RenderGraphBufferState::ShaderRead ||
                 access.state == RenderGraphBufferState::StorageReadWrite) &&
                access.shaderStage != RenderGraphShaderStage::None) {
                name += "(";
                name += shaderStageName(access.shaderStage);
                name += ")";
            }
            return name;
        }

        [[nodiscard]] std::string slotAccessName(RenderGraphSlotAccess access) {
            switch (access) {
            case RenderGraphSlotAccess::ColorWrite:
                return "ColorWrite";
            case RenderGraphSlotAccess::ShaderRead:
                return "ShaderRead";
            case RenderGraphSlotAccess::DepthAttachmentRead:
                return "DepthAttachmentRead";
            case RenderGraphSlotAccess::DepthAttachmentWrite:
                return "DepthAttachmentWrite";
            case RenderGraphSlotAccess::DepthSampledRead:
                return "DepthSampledRead";
            case RenderGraphSlotAccess::TransferRead:
                return "TransferRead";
            case RenderGraphSlotAccess::TransferWrite:
                return "TransferWrite";
            case RenderGraphSlotAccess::BufferShaderRead:
                return "BufferShaderRead";
            case RenderGraphSlotAccess::BufferTransferRead:
                return "BufferTransferRead";
            case RenderGraphSlotAccess::BufferTransferWrite:
                return "BufferTransferWrite";
            case RenderGraphSlotAccess::BufferStorageReadWrite:
                return "BufferStorageReadWrite";
            }
            return "";
        }

        [[nodiscard]] std::string_view commandKindName(RenderGraphCommandKind kind) {
            switch (kind) {
            case RenderGraphCommandKind::SetShader:
                return "SetShader";
            case RenderGraphCommandKind::SetTexture:
                return "SetTexture";
            case RenderGraphCommandKind::SetFloat:
                return "SetFloat";
            case RenderGraphCommandKind::SetInt:
                return "SetInt";
            case RenderGraphCommandKind::SetVec4:
                return "SetVec4";
            case RenderGraphCommandKind::DrawFullscreenTriangle:
                return "DrawFullscreenTriangle";
            case RenderGraphCommandKind::ClearColor:
                return "ClearColor";
            case RenderGraphCommandKind::FillBuffer:
                return "FillBuffer";
            case RenderGraphCommandKind::CopyImage:
                return "CopyImage";
            case RenderGraphCommandKind::CopyBuffer:
                return "CopyBuffer";
            case RenderGraphCommandKind::CopyBufferToImage:
                return "CopyBufferToImage";
            case RenderGraphCommandKind::CopyImageToBuffer:
                return "CopyImageToBuffer";
            case RenderGraphCommandKind::Dispatch:
                return "Dispatch";
            }
            return "";
        }

        [[nodiscard]] std::string_view executionEventKindName(
            BasicRenderViewExecutionEventKind kind) {
            switch (kind) {
            case BasicRenderViewExecutionEventKind::BeginPass:
                return "BeginPass";
            case BasicRenderViewExecutionEventKind::EndPass:
                return "EndPass";
            case BasicRenderViewExecutionEventKind::ClearColor:
                return "ClearColor";
            case BasicRenderViewExecutionEventKind::Draw:
                return "Draw";
            case BasicRenderViewExecutionEventKind::DrawIndexed:
                return "DrawIndexed";
            case BasicRenderViewExecutionEventKind::DrawFullscreenTriangle:
                return "DrawFullscreenTriangle";
            case BasicRenderViewExecutionEventKind::Dispatch:
                return "Dispatch";
            case BasicRenderViewExecutionEventKind::CopyImage:
                return "CopyImage";
            case BasicRenderViewExecutionEventKind::RenderViewInput:
                return "RenderViewInput";
            }
            return "";
        }

        [[nodiscard]] std::string_view transitionPhaseName(
            RenderGraphDiagnosticsTransitionPhase phase) {
            switch (phase) {
            case RenderGraphDiagnosticsTransitionPhase::BeforePass:
                return "BeforePass";
            case RenderGraphDiagnosticsTransitionPhase::Final:
                return "Final";
            }
            return "";
        }

        [[nodiscard]] archive::ArchiveValue extentValue(RenderGraphExtent2D extent) {
            return archive::ArchiveValue::object({
                member("width", uint32Value(extent.width)),
                member("height", uint32Value(extent.height)),
            });
        }

        [[nodiscard]] archive::ArchiveValue captureValue(const EditorFrameDebugCapture& capture) {
            return archive::ArchiveValue::object({
                member("captureId",
                       stringValue("frame:" + std::to_string(capture.submittedFrameEpoch))),
                member("frameIndex", integerValue(capture.frameIndex)),
                member("submittedFrameEpoch", uint64Value(capture.submittedFrameEpoch)),
                member("viewKind", stringValue(viewportKindName(capture.viewKind))),
                member("requestedWidth", uint32Value(capture.requestedExtent.width)),
                member("requestedHeight", uint32Value(capture.requestedExtent.height)),
            });
        }

        [[nodiscard]] archive::ArchiveValue passValue(
            const RenderGraphDiagnosticsPassNode& pass) {
            return archive::ArchiveValue::object({
                member("id", stringValue(passId(pass.passIndex))),
                member("passIndex", countValue(pass.passIndex)),
                member("declarationIndex", countValue(pass.declarationIndex)),
                member("name", stringValue(pass.name)),
                member("type", stringValue(pass.type)),
                member("paramsType", stringValue(pass.paramsType)),
                member("allowCulling", boolValue(pass.allowCulling)),
                member("hasSideEffects", boolValue(pass.hasSideEffects)),
                member("commandCount", countValue(pass.commandCount)),
                member("imageTransitionCount", countValue(pass.imageTransitionCount)),
                member("bufferTransitionCount", countValue(pass.bufferTransitionCount)),
            });
        }

        [[nodiscard]] archive::ArchiveValue commandValue(
            const RenderGraphDiagnosticsCommandNode& command) {
            return archive::ArchiveValue::object({
                member("id", stringValue(commandId(command.passIndex, command.commandIndex))),
                member("passId", stringValue(passId(command.passIndex))),
                member("passName", stringValue(command.passName)),
                member("passIndex", countValue(command.passIndex)),
                member("declarationIndex", countValue(command.declarationIndex)),
                member("commandIndex", countValue(command.commandIndex)),
                member("kind", stringValue(commandKindName(command.kind))),
                member("detail", stringValue(command.detail)),
            });
        }

        [[nodiscard]] archive::ArchiveValue resourceValue(
            const RenderGraphDiagnosticsResourceNode& resource) {
            return archive::ArchiveValue::object({
                member("id", stringValue(resourceId(resource.kind, resource.resourceIndex))),
                member("kind", stringValue(resourceKindName(resource.kind))),
                member("resourceIndex", uint32Value(resource.resourceIndex)),
                member("name", stringValue(resource.name)),
                member("imageFormat", stringValue(imageFormatName(resource.imageFormat))),
                member("imageExtent", extentValue(resource.imageExtent)),
                member("imageInitialAccess",
                       stringValue(imageAccessName(resource.imageInitialAccess))),
                member("imageFinalAccess", stringValue(imageAccessName(resource.imageFinalAccess))),
                member("bufferByteSize", uint64Value(resource.bufferByteSize)),
                member("bufferInitialAccess",
                       stringValue(bufferAccessName(resource.bufferInitialAccess))),
                member("bufferFinalAccess",
                       stringValue(bufferAccessName(resource.bufferFinalAccess))),
            });
        }

        [[nodiscard]] archive::ArchiveValue accessEdgeValue(
            const RenderGraphDiagnosticsAccessEdge& edge) {
            return archive::ArchiveValue::object({
                member("id", stringValue("access:" + std::to_string(edge.passIndex) + ":" +
                                         std::to_string(edge.resourceIndex) + ":" +
                                         edge.slotName)),
                member("passId", stringValue(passId(edge.passIndex))),
                member("passName", stringValue(edge.passName)),
                member("resourceId", stringValue(resourceId(edge.resourceKind, edge.resourceIndex))),
                member("resourceName", stringValue(edge.resourceName)),
                member("slotName", stringValue(edge.slotName)),
                member("access", stringValue(slotAccessName(edge.access))),
                member("shaderStage", stringValue(shaderStageName(edge.shaderStage))),
            });
        }

        [[nodiscard]] archive::ArchiveValue dependencyEdgeValue(
            const RenderGraphDiagnosticsDependencyEdge& edge) {
            return archive::ArchiveValue::object({
                member("id", stringValue("dependency:" + std::to_string(edge.fromPassIndex) +
                                         ":" + std::to_string(edge.toPassIndex) + ":" +
                                         std::to_string(edge.resourceIndex))),
                member("fromPassId", stringValue(passId(edge.fromPassIndex))),
                member("toPassId", stringValue(passId(edge.toPassIndex))),
                member("resourceId", stringValue(resourceId(edge.resourceKind, edge.resourceIndex))),
                member("resourceName", stringValue(edge.resourceName)),
                member("reason", stringValue(edge.reason)),
            });
        }

        [[nodiscard]] archive::ArchiveValue transitionValue(
            const RenderGraphDiagnosticsTransition& transition) {
            return archive::ArchiveValue::object({
                member("id", stringValue("transition:" +
                                         std::string{transitionPhaseName(transition.phase)} + ":" +
                                         std::to_string(transition.passIndex) + ":" +
                                         std::to_string(transition.resourceIndex))),
                member("phase", stringValue(transitionPhaseName(transition.phase))),
                member("passId", stringValue(passId(transition.passIndex))),
                member("passName", stringValue(transition.passName)),
                member("resourceId",
                       stringValue(resourceId(transition.resourceKind,
                                              transition.resourceIndex))),
                member("resourceName", stringValue(transition.resourceName)),
                member("oldImageAccess", stringValue(imageAccessName(transition.oldImageAccess))),
                member("newImageAccess", stringValue(imageAccessName(transition.newImageAccess))),
                member("oldBufferAccess",
                       stringValue(bufferAccessName(transition.oldBufferAccess))),
                member("newBufferAccess",
                       stringValue(bufferAccessName(transition.newBufferAccess))),
            });
        }

        [[nodiscard]] archive::ArchiveValue executionEventValue(
            const BasicRenderViewExecutionEvent& event) {
            return archive::ArchiveValue::object({
                member("id", stringValue(eventId(event.id))),
                member("eventIndex", uint64Value(event.id.value)),
                member("kind", stringValue(executionEventKindName(event.kind))),
                member("passId", stringValue(passId(event.passIndex))),
                member("passName", stringValue(event.passName)),
                member("commandId",
                       event.commandIndex
                           ? stringValue(commandId(event.passIndex, *event.commandIndex))
                           : archive::ArchiveValue::null()),
                member("label", stringValue(event.label)),
                member("sourceResourceId",
                       event.sourceImageResourceIndex
                           ? stringValue(resourceId(RenderGraphResourceKind::Image,
                                                    *event.sourceImageResourceIndex))
                           : archive::ArchiveValue::null()),
                member("targetResourceId",
                       event.targetImageResourceIndex
                           ? stringValue(resourceId(RenderGraphResourceKind::Image,
                                                    *event.targetImageResourceIndex))
                           : archive::ArchiveValue::null()),
                member("vertexCount", uint32Value(event.draw.vertexCount)),
                member("indexCount", uint32Value(event.draw.indexCount)),
                member("instanceCount", uint32Value(event.draw.instanceCount)),
                member("groupCountX", uint32Value(event.dispatch.groupCountX)),
                member("groupCountY", uint32Value(event.dispatch.groupCountY)),
                member("groupCountZ", uint32Value(event.dispatch.groupCountZ)),
            });
        }

        template <typename Source, typename Projector>
        [[nodiscard]] archive::ArchiveValue arrayValue(const Source& source, Projector projector) {
            std::vector<archive::ArchiveValue> values;
            values.reserve(source.size());
            for (const auto& item : source) {
                values.push_back(projector(item));
            }
            return archive::ArchiveValue::array(std::move(values));
        }

        [[nodiscard]] archive::ArchiveValue previewValue(const EditorFrameDebugPreview& preview) {
            return archive::ArchiveValue::object({
                member("status", stringValue(editorFrameDebugPreviewStatusName(preview.status))),
                member("selectedPassId",
                       preview.selectedPassIndex ? stringValue(passId(*preview.selectedPassIndex))
                                                 : archive::ArchiveValue::null()),
                member("selectedExecutionEventId",
                       preview.selectedExecutionEventId
                           ? stringValue(eventId(*preview.selectedExecutionEventId))
                           : archive::ArchiveValue::null()),
                member("sourceResourceId",
                       preview.selectedImageResourceIndex
                           ? stringValue(resourceId(RenderGraphResourceKind::Image,
                                                    *preview.selectedImageResourceIndex))
                           : archive::ArchiveValue::null()),
                member("sourceResourceIndex",
                       optionalUint32Value(preview.selectedImageResourceIndex)),
                member("copiedAfterPassIndex", optionalSizeValue(preview.copiedAfterPassIndex)),
                member("message", stringValue(preview.message)),
            });
        }

        [[nodiscard]] archive::ArchiveValue snapshotValue(
            const EditorFrameDebugger& frameDebugger, const EditorFrameDebugCapture* capture) {
            const RenderGraphDiagnosticsSnapshot* renderGraph = nullptr;
            const BasicRenderViewDiagnostics* diagnostics = nullptr;
            if (capture != nullptr) {
                diagnostics = &capture->diagnostics;
                renderGraph = &diagnostics->renderGraph;
            }

            return archive::ArchiveValue::object({
                member("schemaVersion", integerValue(1)),
                member("version", integerValue(1)),
                member("state", stringValue(frameDebugger.stateName())),
                member("capture",
                       capture == nullptr ? archive::ArchiveValue::null()
                                          : captureValue(*capture)),
                member("passes",
                       renderGraph == nullptr
                           ? archive::ArchiveValue::array({})
                           : arrayValue(renderGraph->passes, passValue)),
                member("commands",
                       renderGraph == nullptr
                           ? archive::ArchiveValue::array({})
                           : arrayValue(renderGraph->commands, commandValue)),
                member("resources",
                       renderGraph == nullptr
                           ? archive::ArchiveValue::array({})
                           : arrayValue(renderGraph->resources, resourceValue)),
                member("accessEdges",
                       renderGraph == nullptr
                           ? archive::ArchiveValue::array({})
                           : arrayValue(renderGraph->accessEdges, accessEdgeValue)),
                member("dependencyEdges",
                       renderGraph == nullptr
                           ? archive::ArchiveValue::array({})
                           : arrayValue(renderGraph->dependencyEdges, dependencyEdgeValue)),
                member("transitions",
                       renderGraph == nullptr
                           ? archive::ArchiveValue::array({})
                           : arrayValue(renderGraph->transitions, transitionValue)),
                member("executionEvents",
                       diagnostics == nullptr
                           ? archive::ArchiveValue::array({})
                           : arrayValue(diagnostics->executionEvents, executionEventValue)),
                member("preview", previewValue(frameDebugger.preview())),
                member("message", stringValue(capture == nullptr ? "No frame debug capture."
                                                                  : "")),
            });
        }

    } // namespace

    Result<std::string> writeFrameDebuggerSnapshotJson(const EditorFrameDebugger& frameDebugger) {
        const std::optional<EditorFrameDebugCapture>& paused = frameDebugger.pausedCapture();
        const std::optional<EditorFrameDebugCapture>& latest = frameDebugger.latestCapture();
        const EditorFrameDebugCapture* capture = nullptr;
        if (paused) {
            capture = &(*paused);
        } else if (latest) {
            capture = &(*latest);
        }

        return archive::writeJsonArchive(snapshotValue(frameDebugger, capture));
    }

    Result<std::string>
    writeStudioFrameDebuggerSnapshotJson(const EditorFrameDebugger& frameDebugger) {
        return writeFrameDebuggerSnapshotJson(frameDebugger);
    }
} // namespace asharia::editor
