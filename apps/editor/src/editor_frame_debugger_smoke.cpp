#include "editor_frame_debugger_smoke.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/core/log.hpp"
#include "asharia/renderer_basic/render_graph_schemas.hpp"

#include "editor_frame_debugger.hpp"
#include "editor_frame_debugger_snapshot_projector.hpp"
#include "editor_smoke.hpp"
#include "editor_viewport_overlay_provider.hpp"

namespace asharia::editor {
    namespace {

        [[nodiscard]] bool
        capturedSourceOverlayId(const asharia::BasicRenderViewOverlayDiagnostics& overlay,
                                std::string_view sourceOverlayId) {
            return std::ranges::find(overlay.sourceOverlayIds, sourceOverlayId) !=
                   overlay.sourceOverlayIds.end();
        }

        [[nodiscard]] const asharia::BasicRenderViewExecutionEvent*
        capturedExecutionEvent(const asharia::BasicRenderViewDiagnostics& diagnostics,
                               std::uint64_t eventId) {
            for (const asharia::BasicRenderViewExecutionEvent& event :
                 diagnostics.executionEvents) {
                if (event.id.value == eventId) {
                    return &event;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool
        isInspectableExecutionEvent(asharia::BasicRenderViewExecutionEventKind kind) {
            return kind != asharia::BasicRenderViewExecutionEventKind::BeginPass &&
                   kind != asharia::BasicRenderViewExecutionEventKind::EndPass;
        }

        [[nodiscard]] const asharia::BasicRenderViewExecutionEvent*
        capturedStructuralExecutionEvent(const asharia::BasicRenderViewDiagnostics& diagnostics) {
            for (const asharia::BasicRenderViewExecutionEvent& event :
                 diagnostics.executionEvents) {
                if (!isInspectableExecutionEvent(event.kind)) {
                    return &event;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool isPreviewableWriteAccess(asharia::RenderGraphSlotAccess access) {
            return access == asharia::RenderGraphSlotAccess::ColorWrite ||
                   access == asharia::RenderGraphSlotAccess::TransferWrite;
        }

        [[nodiscard]] bool
        capturedWriteAccessForPassResource(const asharia::RenderGraphDiagnosticsSnapshot& snapshot,
                                           std::size_t passIndex, std::uint32_t resourceIndex) {
            return std::ranges::any_of(
                snapshot.accessEdges,
                [passIndex, resourceIndex](const asharia::RenderGraphDiagnosticsAccessEdge& edge) {
                    return edge.passIndex == passIndex &&
                           edge.resourceKind == asharia::RenderGraphResourceKind::Image &&
                           edge.resourceIndex == resourceIndex &&
                           isPreviewableWriteAccess(edge.access);
                });
        }

        [[nodiscard]] bool validateSelectedPreviewEventMapping(
            const EditorSmokeRunResult& runResult,
            const asharia::BasicRenderViewDiagnostics& diagnostics) {
            if (!runResult.frameDebugPreviewSelectedPassIndex ||
                !runResult.frameDebugPreviewSelectedExecutionEventId ||
                !runResult.frameDebugPreviewSelectedImageResourceIndex ||
                !runResult.frameDebugPreviewCopiedAfterPassIndex ||
                *runResult.frameDebugPreviewSelectedPassIndex !=
                    *runResult.frameDebugPreviewCopiedAfterPassIndex) {
                asharia::logError(
                    "Editor frame debugger smoke did not copy preview after the selected pass.");
                return false;
            }

            const asharia::BasicRenderViewExecutionEvent* selectedEvent = capturedExecutionEvent(
                diagnostics, *runResult.frameDebugPreviewSelectedExecutionEventId);
            if (selectedEvent == nullptr) {
                asharia::logError(
                    "Editor frame debugger smoke selected an event missing from the capture.");
                return false;
            }
            if (!isInspectableExecutionEvent(selectedEvent->kind) ||
                selectedEvent->passIndex != *runResult.frameDebugPreviewSelectedPassIndex) {
                asharia::logError(
                    "Editor frame debugger smoke selected an event that does not map to the "
                    "previewed pass.");
                return false;
            }
            if (!selectedEvent->targetImageResourceIndex ||
                *selectedEvent->targetImageResourceIndex !=
                    *runResult.frameDebugPreviewSelectedImageResourceIndex) {
                asharia::logError(
                    "Editor frame debugger smoke selected an event whose target image does not "
                    "map to the previewed resource.");
                return false;
            }
            if (!capturedWriteAccessForPassResource(diagnostics.renderGraph,
                                                    selectedEvent->passIndex,
                                                    *selectedEvent->targetImageResourceIndex)) {
                asharia::logError(
                    "Editor frame debugger smoke selected an event whose target image is not a "
                    "captured RenderGraph write output for the pass.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        validateStructuralEventReplayUnavailable(const EditorFrameDebugCapture& capture) {
            const asharia::BasicRenderViewExecutionEvent* structuralEvent =
                capturedStructuralExecutionEvent(capture.diagnostics);
            if (structuralEvent == nullptr) {
                asharia::logError(
                    "Editor frame debugger smoke captured no structural execution event.");
                return false;
            }

            EditorFrameDebugger debugger;
            if (!debugger.requestCapture()) {
                asharia::logError(
                    "Editor frame debugger smoke could not request temporary capture.");
                return false;
            }
            debugger.beginFrame(capture.frameIndex);
            debugger.captureRecordedView(EditorFrameDebugCaptureDesc{
                .frameIndex = capture.frameIndex,
                .submittedFrameEpoch = capture.submittedFrameEpoch,
                .viewKind = capture.viewKind,
                .requestedExtent = capture.requestedExtent,
                .diagnostics = capture.diagnostics,
            });
            debugger.endSubmittedFrame(capture.submittedFrameEpoch);
            if (debugger.state() != EditorFrameDebuggerState::PausedFrameDebug ||
                !debugger.selectReplayEvent(structuralEvent->id)) {
                asharia::logError(
                    "Editor frame debugger smoke could not select a structural event.");
                return false;
            }

            const EditorFrameDebugPreview& preview = debugger.preview();
            if (preview.status != EditorFrameDebugPreviewStatus::Unavailable || preview.dirty ||
                preview.selectedImageResourceIndex || debugger.consumePreviewRequest()) {
                asharia::logError(
                    "Editor frame debugger smoke allowed a structural event preview request.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool closeFloat(float lhs, float rhs) {
            return std::fabs(lhs - rhs) < 0.0001F;
        }

        [[nodiscard]] bool
        capturedWorldGridSettings(const asharia::BasicRenderViewOverlayDiagnostics& overlay) {
            const asharia::BasicRenderViewWorldGridDesc& worldGrid = overlay.worldGrid;
            const EditorViewportWorldGridSettings expected = defaultEditorSceneGridSettings();
            return overlay.worldGridEnabled && worldGrid.enabled &&
                   closeFloat(worldGrid.planeY, expected.planeY) &&
                   closeFloat(worldGrid.minorSpacing, expected.minorSpacing) &&
                   closeFloat(worldGrid.majorSpacing, expected.majorSpacing) &&
                   closeFloat(worldGrid.fadeStart, expected.fadeStart) &&
                   closeFloat(worldGrid.fadeEnd, expected.fadeEnd) &&
                   closeFloat(worldGrid.opacity, expected.opacity) &&
                   closeFloat(worldGrid.color[0], expected.color[0]) &&
                   closeFloat(worldGrid.color[1], expected.color[1]) &&
                   closeFloat(worldGrid.color[2], expected.color[2]) &&
                   closeFloat(worldGrid.color[3], expected.color[3]);
        }

        [[nodiscard]] const asharia::archive::ArchiveValue*
        requiredObjectMember(const asharia::archive::ArchiveValue& value, std::string_view key) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            if (member == nullptr || member->kind != asharia::archive::ArchiveValueKind::Object) {
                return nullptr;
            }
            return member;
        }

        [[nodiscard]] const asharia::archive::ArchiveValue*
        requiredArrayMember(const asharia::archive::ArchiveValue& value, std::string_view key) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            if (member == nullptr || member->kind != asharia::archive::ArchiveValueKind::Array) {
                return nullptr;
            }
            return member;
        }

        [[nodiscard]] bool requiredStringMemberEquals(
            const asharia::archive::ArchiveValue& value, std::string_view key,
            const char* expected) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            return member != nullptr && member->kind == asharia::archive::ArchiveValueKind::String &&
                   member->stringValue == expected;
        }

        [[nodiscard]] bool arrayContainsStringMember(
            const asharia::archive::ArchiveValue& value, std::string_view key,
            const char* expected) {
            return value.kind == asharia::archive::ArchiveValueKind::Array &&
                   std::ranges::any_of(
                       value.arrayValue,
                       [key, expected](const asharia::archive::ArchiveValue& item) {
                           return requiredStringMemberEquals(item, key, expected);
                       });
        }

        [[nodiscard]] bool requiredIntegerMemberEquals(
            const asharia::archive::ArchiveValue& value, std::string_view key,
            std::int64_t expected) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            return member != nullptr &&
                   member->kind == asharia::archive::ArchiveValueKind::Integer &&
                   member->integerValue == expected;
        }

        [[nodiscard]] bool requiredNullMember(const asharia::archive::ArchiveValue& value,
                                              std::string_view key) {
            const asharia::archive::ArchiveValue* member = value.findMemberValue(key);
            return member != nullptr && member->kind == asharia::archive::ArchiveValueKind::Null;
        }

        [[nodiscard]] const asharia::archive::ArchiveValue* arrayObjectWithStringMember(
            const asharia::archive::ArchiveValue& value, std::string_view key,
            const char* expected) {
            if (value.kind != asharia::archive::ArchiveValueKind::Array) {
                return nullptr;
            }
            const auto found = std::ranges::find_if(
                value.arrayValue, [key, expected](const asharia::archive::ArchiveValue& item) {
                    return requiredStringMemberEquals(item, key, expected);
                });
            return found == value.arrayValue.end() ? nullptr : &(*found);
        }

        constexpr std::uint32_t kProjectedSceneColorResourceIndex = 17U;
        constexpr std::uint32_t kProjectedSceneDepthResourceIndex = 18U;
        constexpr std::uint32_t kProjectedSceneVertexResourceIndex = 27U;
        constexpr std::uint32_t kProjectedSceneIndexResourceIndex = 28U;
        constexpr std::size_t kProjectedSceneDrawItemIndex = 6U;
        constexpr asharia::BasicDrawSourceId kProjectedSceneSourceObject{
            .index = 0x12345678U,
            .generation = 0x9ABCDEF0U,
        };
        constexpr asharia::BasicDrawResourceKey kProjectedSceneMeshResourceKey{
            .value = 0x0123456789ABCDEFULL,
        };
        constexpr asharia::BasicDrawResourceKey kProjectedSceneMaterialResourceKey{
            .value = 0xFEDCBA9876543210ULL,
        };
        constexpr std::size_t kProjectedSceneCommandIndex = 4U;
        constexpr std::uint32_t kProjectedSceneIndexCount = 72U;
        constexpr std::uint32_t kProjectedSceneInstanceCount = 3U;
        constexpr std::uint32_t kProjectedSceneFirstVertex = 11U;
        constexpr std::uint32_t kProjectedSceneFirstIndex = 5U;
        constexpr std::int32_t kProjectedSceneVertexOffset = -2;
        constexpr std::uint32_t kProjectedSceneFirstInstance = 9U;

        [[nodiscard]] asharia::BasicRenderViewExecutionEventId
        appendSceneDrawProjectionFixture(EditorFrameDebugCapture& capture) {
            asharia::RenderGraphDiagnosticsSnapshot& graph = capture.diagnostics.renderGraph;
            const std::size_t scenePassIndex = graph.passes.size();
            const std::size_t sceneDeclarationIndex = graph.declaredPassCount;

            asharia::RenderGraphDiagnosticsPassNode scenePass;
            scenePass.passIndex = scenePassIndex;
            scenePass.declarationIndex = sceneDeclarationIndex;
            scenePass.name = "RenderViewSceneMesh";
            scenePass.type = asharia::kBasicRenderViewSceneMeshPassType;
            scenePass.paramsType = asharia::kBasicRenderViewSceneMeshParamsType;
            scenePass.commandCount = 1U;
            graph.passes.push_back(std::move(scenePass));
            ++graph.declaredPassCount;

            asharia::RenderGraphDiagnosticsResourceNode color;
            color.resourceIndex = kProjectedSceneColorResourceIndex;
            color.name = "SceneMeshColor";
            color.imageFormat = asharia::RenderGraphImageFormat::B8G8R8A8Srgb;
            color.imageExtent.width = capture.requestedExtent.width;
            color.imageExtent.height = capture.requestedExtent.height;
            color.imageFinalAccess.state = asharia::RenderGraphImageState::ColorAttachment;
            graph.resources.push_back(std::move(color));

            asharia::RenderGraphDiagnosticsResourceNode depth;
            depth.resourceIndex = kProjectedSceneDepthResourceIndex;
            depth.name = "SceneMeshDepth";
            depth.imageFormat = asharia::RenderGraphImageFormat::D32Sfloat;
            depth.imageExtent.width = capture.requestedExtent.width;
            depth.imageExtent.height = capture.requestedExtent.height;
            depth.imageFinalAccess.state = asharia::RenderGraphImageState::DepthAttachmentWrite;
            graph.resources.push_back(std::move(depth));
            graph.declaredImageCount += 2U;

            asharia::RenderGraphDiagnosticsResourceNode vertices;
            vertices.kind = asharia::RenderGraphResourceKind::Buffer;
            vertices.resourceIndex = kProjectedSceneVertexResourceIndex;
            vertices.name = "ValidationMeshVertices";
            vertices.bufferByteSize = 14U * sizeof(asharia::BasicVertex3D);
            vertices.bufferInitialAccess.state = asharia::RenderGraphBufferState::VertexRead;
            vertices.bufferFinalAccess = vertices.bufferInitialAccess;
            graph.resources.push_back(std::move(vertices));

            asharia::RenderGraphDiagnosticsResourceNode indices;
            indices.kind = asharia::RenderGraphResourceKind::Buffer;
            indices.resourceIndex = kProjectedSceneIndexResourceIndex;
            indices.name = "ValidationMeshIndices";
            indices.bufferByteSize = 72U * sizeof(std::uint16_t);
            indices.bufferInitialAccess.state = asharia::RenderGraphBufferState::IndexRead;
            indices.bufferFinalAccess = indices.bufferInitialAccess;
            graph.resources.push_back(std::move(indices));
            graph.declaredBufferCount += 2U;

            asharia::RenderGraphDiagnosticsAccessEdge colorAccess;
            colorAccess.passIndex = scenePassIndex;
            colorAccess.declarationIndex = sceneDeclarationIndex;
            colorAccess.passName = "RenderViewSceneMesh";
            colorAccess.resourceIndex = kProjectedSceneColorResourceIndex;
            colorAccess.resourceName = "SceneMeshColor";
            colorAccess.slotName = "color";
            colorAccess.access = asharia::RenderGraphSlotAccess::ColorWrite;
            graph.accessEdges.push_back(std::move(colorAccess));

            asharia::RenderGraphDiagnosticsAccessEdge depthAccess;
            depthAccess.passIndex = scenePassIndex;
            depthAccess.declarationIndex = sceneDeclarationIndex;
            depthAccess.passName = "RenderViewSceneMesh";
            depthAccess.resourceIndex = kProjectedSceneDepthResourceIndex;
            depthAccess.resourceName = "SceneMeshDepth";
            depthAccess.slotName = "depth";
            depthAccess.access = asharia::RenderGraphSlotAccess::DepthAttachmentWrite;
            graph.accessEdges.push_back(std::move(depthAccess));

            asharia::RenderGraphDiagnosticsAccessEdge vertexAccess;
            vertexAccess.passIndex = scenePassIndex;
            vertexAccess.declarationIndex = sceneDeclarationIndex;
            vertexAccess.passName = "RenderViewSceneMesh";
            vertexAccess.resourceKind = asharia::RenderGraphResourceKind::Buffer;
            vertexAccess.resourceIndex = kProjectedSceneVertexResourceIndex;
            vertexAccess.resourceName = "ValidationMeshVertices";
            vertexAccess.slotName = "vertices";
            vertexAccess.access = asharia::RenderGraphSlotAccess::BufferVertexRead;
            graph.accessEdges.push_back(std::move(vertexAccess));

            asharia::RenderGraphDiagnosticsAccessEdge indexAccess;
            indexAccess.passIndex = scenePassIndex;
            indexAccess.declarationIndex = sceneDeclarationIndex;
            indexAccess.passName = "RenderViewSceneMesh";
            indexAccess.resourceKind = asharia::RenderGraphResourceKind::Buffer;
            indexAccess.resourceIndex = kProjectedSceneIndexResourceIndex;
            indexAccess.resourceName = "ValidationMeshIndices";
            indexAccess.slotName = "indices";
            indexAccess.access = asharia::RenderGraphSlotAccess::BufferIndexRead;
            graph.accessEdges.push_back(std::move(indexAccess));

            asharia::RenderGraphDiagnosticsCommandNode drawCommand;
            drawCommand.passIndex = scenePassIndex;
            drawCommand.declarationIndex = sceneDeclarationIndex;
            drawCommand.commandIndex = kProjectedSceneCommandIndex;
            drawCommand.passName = "RenderViewSceneMesh";
            drawCommand.kind = asharia::RenderGraphCommandKind::DrawIndexed;
            drawCommand.detail =
                "indexCount=72, instanceCount=3, firstIndex=5, vertexOffset=-2, "
                "firstInstance=9";
            graph.commands.push_back(std::move(drawCommand));

            asharia::BasicRenderViewExecutionEvent drawEvent;
            drawEvent.id.value = capture.diagnostics.executionEvents.size() + 1U;
            drawEvent.kind = asharia::BasicRenderViewExecutionEventKind::DrawIndexed;
            drawEvent.passIndex = scenePassIndex;
            drawEvent.declarationIndex = sceneDeclarationIndex;
            drawEvent.commandIndex = kProjectedSceneCommandIndex;
            drawEvent.passName = "RenderViewSceneMesh";
            drawEvent.label = "DrawSceneMeshIndexed";
            drawEvent.draw.indexCount = kProjectedSceneIndexCount;
            drawEvent.draw.instanceCount = kProjectedSceneInstanceCount;
            drawEvent.draw.firstVertex = kProjectedSceneFirstVertex;
            drawEvent.draw.firstIndex = kProjectedSceneFirstIndex;
            drawEvent.draw.vertexOffset = kProjectedSceneVertexOffset;
            drawEvent.draw.firstInstance = kProjectedSceneFirstInstance;
            drawEvent.sceneDrawItemIndex = kProjectedSceneDrawItemIndex;
            asharia::BasicDrawPacketContext packetContext;
            packetContext.sourceObject = kProjectedSceneSourceObject;
            packetContext.meshResource = kProjectedSceneMeshResourceKey;
            packetContext.materialResource = kProjectedSceneMaterialResourceKey;
            drawEvent.drawPacketContext = packetContext;
            drawEvent.targetImageResourceIndex = kProjectedSceneColorResourceIndex;
            drawEvent.depthImageResourceIndex = kProjectedSceneDepthResourceIndex;
            drawEvent.vertexBufferResourceIndex = kProjectedSceneVertexResourceIndex;
            drawEvent.indexBufferResourceIndex = kProjectedSceneIndexResourceIndex;
            const asharia::BasicRenderViewExecutionEventId eventId = drawEvent.id;
            capture.diagnostics.executionEvents.push_back(std::move(drawEvent));
            return eventId;
        }

        [[nodiscard]] bool validateProjectedSceneDrawEvent(
            const asharia::archive::ArchiveValue& events,
            const asharia::BasicRenderViewExecutionEvent& expectedEvent) {
            const asharia::archive::ArchiveValue* event =
                arrayObjectWithStringMember(events, "label", "DrawSceneMeshIndexed");
            if (event == nullptr) {
                return false;
            }
            const asharia::archive::ArchiveValue* sourceObject =
                requiredObjectMember(*event, "sourceObject");
            const std::string commandId =
                "command:" + std::to_string(expectedEvent.passIndex) + ":" +
                std::to_string(kProjectedSceneCommandIndex);
            const std::string targetResourceId =
                "image:" + std::to_string(kProjectedSceneColorResourceIndex);
            const std::string depthResourceId =
                "image:" + std::to_string(kProjectedSceneDepthResourceIndex);
            const std::string vertexResourceId =
                "buffer:" + std::to_string(kProjectedSceneVertexResourceIndex);
            const std::string indexResourceId =
                "buffer:" + std::to_string(kProjectedSceneIndexResourceIndex);
            return sourceObject != nullptr &&
                   requiredStringMemberEquals(*event, "kind", "DrawIndexed") &&
                   requiredStringMemberEquals(*event, "passName", "RenderViewSceneMesh") &&
                   requiredIntegerMemberEquals(
                       *event, "declarationIndex",
                       static_cast<std::int64_t>(expectedEvent.declarationIndex)) &&
                   requiredIntegerMemberEquals(
                       *event, "commandIndex",
                       static_cast<std::int64_t>(kProjectedSceneCommandIndex)) &&
                   requiredStringMemberEquals(*event, "commandId", commandId.c_str()) &&
                   requiredIntegerMemberEquals(
                       *event, "sceneDrawItemIndex",
                       static_cast<std::int64_t>(kProjectedSceneDrawItemIndex)) &&
                   requiredIntegerMemberEquals(*sourceObject, "index",
                                               kProjectedSceneSourceObject.index) &&
                   requiredIntegerMemberEquals(*sourceObject, "generation",
                                               kProjectedSceneSourceObject.generation) &&
                   requiredStringMemberEquals(*event, "meshResourceKey",
                                              "0123456789abcdef") &&
                   requiredStringMemberEquals(*event, "materialResourceKey",
                                              "fedcba9876543210") &&
                   requiredNullMember(*event, "sourceResourceId") &&
                   requiredStringMemberEquals(*event, "targetResourceId",
                                              targetResourceId.c_str()) &&
                   requiredStringMemberEquals(*event, "depthResourceId",
                                              depthResourceId.c_str()) &&
                   requiredStringMemberEquals(*event, "vertexResourceId",
                                              vertexResourceId.c_str()) &&
                   requiredStringMemberEquals(*event, "indexResourceId",
                                              indexResourceId.c_str()) &&
                   requiredIntegerMemberEquals(*event, "vertexCount", 0) &&
                   requiredIntegerMemberEquals(*event, "indexCount",
                                               kProjectedSceneIndexCount) &&
                   requiredIntegerMemberEquals(*event, "instanceCount",
                                               kProjectedSceneInstanceCount) &&
                   requiredIntegerMemberEquals(*event, "firstVertex",
                                               kProjectedSceneFirstVertex) &&
                   requiredIntegerMemberEquals(*event, "firstIndex",
                                               kProjectedSceneFirstIndex) &&
                   requiredIntegerMemberEquals(*event, "vertexOffset",
                                               kProjectedSceneVertexOffset) &&
                   requiredIntegerMemberEquals(*event, "firstInstance",
                                               kProjectedSceneFirstInstance);
        }

        [[nodiscard]] bool validateProjectedStructuralEventOptionals(
            const asharia::archive::ArchiveValue& events) {
            const asharia::archive::ArchiveValue* event =
                arrayObjectWithStringMember(events, "kind", "BeginPass");
            return event != nullptr && requiredNullMember(*event, "commandIndex") &&
                   requiredNullMember(*event, "commandId") &&
                   requiredNullMember(*event, "sceneDrawItemIndex") &&
                   requiredNullMember(*event, "sourceObject") &&
                   requiredNullMember(*event, "meshResourceKey") &&
                   requiredNullMember(*event, "materialResourceKey") &&
                   requiredNullMember(*event, "depthResourceId") &&
                   requiredNullMember(*event, "vertexResourceId") &&
                   requiredNullMember(*event, "indexResourceId");
        }

        [[nodiscard]] bool validateStudioFrameDebugSnapshotProjection(
            const EditorFrameDebugCapture& capture) {
            EditorFrameDebugCapture projectionCapture = capture;
            const asharia::BasicRenderViewExecutionEventId sceneDrawEventId =
                appendSceneDrawProjectionFixture(projectionCapture);
            const asharia::BasicRenderViewExecutionEvent* selectedEvent =
                capturedExecutionEvent(projectionCapture.diagnostics, sceneDrawEventId.value);
            if (selectedEvent == nullptr) {
                asharia::logError(
                    "Editor frame debugger smoke found no previewable event for Studio snapshot.");
                return false;
            }

            EditorFrameDebugger projectedDebugger;
            if (!projectedDebugger.requestCapture()) {
                asharia::logError(
                    "Editor frame debugger smoke could not request projection capture.");
                return false;
            }
            projectedDebugger.beginFrame(projectionCapture.frameIndex);
            projectedDebugger.captureRecordedView(EditorFrameDebugCaptureDesc{
                .frameIndex = projectionCapture.frameIndex,
                .submittedFrameEpoch = projectionCapture.submittedFrameEpoch,
                .viewKind = projectionCapture.viewKind,
                .requestedExtent = projectionCapture.requestedExtent,
                .diagnostics = projectionCapture.diagnostics,
            });
            projectedDebugger.endSubmittedFrame(projectionCapture.submittedFrameEpoch);
            if (projectedDebugger.state() != EditorFrameDebuggerState::PausedFrameDebug ||
                !projectedDebugger.selectReplayEvent(selectedEvent->id) ||
                !projectedDebugger.preview().selectedImageResourceIndex) {
                asharia::logError(
                    "Editor frame debugger smoke could not prepare projected paused snapshot.");
                return false;
            }

            auto json = writeFrameDebuggerSnapshotJson(projectedDebugger);
            if (!json) {
                asharia::logError("Editor frame debugger smoke could not write Studio snapshot.");
                return false;
            }

            auto parsed = asharia::archive::readJsonArchive(*json);
            if (!parsed) {
                asharia::logError("Editor frame debugger smoke wrote invalid Studio snapshot JSON.");
                return false;
            }

            const asharia::archive::ArchiveValue& root = *parsed;
            const asharia::archive::ArchiveValue* captureJson =
                requiredObjectMember(root, "capture");
            const asharia::archive::ArchiveValue* passes = requiredArrayMember(root, "passes");
            const asharia::archive::ArchiveValue* resources =
                requiredArrayMember(root, "resources");
            const asharia::archive::ArchiveValue* events =
                requiredArrayMember(root, "executionEvents");
            const asharia::archive::ArchiveValue* preview =
                requiredObjectMember(root, "preview");
            if (!requiredIntegerMemberEquals(root, "schemaVersion", 1) ||
                !requiredIntegerMemberEquals(root, "version", 1) ||
                !requiredStringMemberEquals(root, "state", "PausedFrameDebug") ||
                captureJson == nullptr || passes == nullptr || resources == nullptr ||
                events == nullptr || preview == nullptr) {
                asharia::logError(
                    "Editor frame debugger smoke wrote an incomplete Studio snapshot root.");
                return false;
            }
            if (!requiredIntegerMemberEquals(*captureJson, "frameIndex",
                                             projectionCapture.frameIndex) ||
                !requiredIntegerMemberEquals(
                    *captureJson, "submittedFrameEpoch",
                    static_cast<std::int64_t>(projectionCapture.submittedFrameEpoch)) ||
                !requiredStringMemberEquals(*captureJson, "viewKind", "Scene")) {
                asharia::logError(
                    "Editor frame debugger smoke wrote an incomplete Studio capture snapshot.");
                return false;
            }
            if (passes->arrayValue.size() !=
                    projectionCapture.diagnostics.renderGraph.passes.size() ||
                resources->arrayValue.size() !=
                    projectionCapture.diagnostics.renderGraph.resources.size() ||
                events->arrayValue.size() !=
                    projectionCapture.diagnostics.executionEvents.size()) {
                asharia::logError(
                    "Editor frame debugger smoke projected unexpected Studio snapshot counts.");
                return false;
            }
            const asharia::archive::ArchiveValue* commands =
                requiredArrayMember(root, "commands");
            const asharia::archive::ArchiveValue* accesses =
                requiredArrayMember(root, "accessEdges");
            if (commands == nullptr || accesses == nullptr ||
                !arrayContainsStringMember(*resources, "bufferInitialAccess", "VertexRead") ||
                !arrayContainsStringMember(*resources, "bufferInitialAccess", "IndexRead") ||
                !arrayContainsStringMember(*accesses, "access", "BufferVertexRead") ||
                !arrayContainsStringMember(*accesses, "access", "BufferIndexRead") ||
                !arrayContainsStringMember(*commands, "kind", "DrawIndexed") ||
                !arrayContainsStringMember(*events, "kind", "DrawIndexed") ||
                !validateProjectedSceneDrawEvent(*events, *selectedEvent) ||
                !validateProjectedStructuralEventOptionals(*events)) {
                asharia::logError(
                    "Editor frame debugger smoke lost Scene mesh packet/resource/draw fields.");
                return false;
            }
            if (!requiredStringMemberEquals(*preview, "status", "Pending") ||
                !requiredIntegerMemberEquals(
                    *preview, "sourceResourceIndex", kProjectedSceneColorResourceIndex)) {
                asharia::logError(
                    "Editor frame debugger smoke did not project preview metadata.");
                return false;
            }
            return true;
        }

    } // namespace

    [[nodiscard]] bool validateFrameDebuggerSmoke(EditorRunMode mode,
                                                  const EditorSmokeRunResult& runResult,
                                                  const EditorFrameDebugger& frameDebugger) {
        if (!isEditorFrameDebuggerSmokeMode(mode)) {
            return true;
        }

        if (!runResult.frameDebugCaptureRequested || !runResult.frameDebugReplayPassRequested ||
            !runResult.frameDebugPreviewRequested || !runResult.frameDebugPreviewVisible ||
            !runResult.frameDebugResumeRequested || !runResult.frameDebugRenderedAfterResume) {
            asharia::logError(
                "Editor frame debugger smoke did not complete capture/preview/resume flow.");
            return false;
        }
        if (frameDebugger.state() != EditorFrameDebuggerState::Running) {
            asharia::logError("Editor frame debugger smoke did not return to Running state.");
            return false;
        }

        const EditorFrameDebuggerStats stats = frameDebugger.stats();
        if (stats.captureRequests != 1 || stats.framesCaptured != 1 ||
            stats.completedCaptures != 1 || stats.resumeRequests != 1 || stats.framesResumed != 1 ||
            stats.renderViewFramesSkipped == 0 || stats.previewRequests == 0 ||
            stats.previewFramesRecorded == 0 || stats.previewTextureFramesPublished == 0 ||
            stats.previewTextureFramesDrawn == 0 || stats.replayPassRequests == 0 ||
            stats.replayPassSelections == 0 || stats.replayEventRequests == 0 ||
            stats.replayEventSelections == 0 || stats.frameDebugRenderGraphViewFrames == 0 ||
            stats.frameDebugRenderGraphSnapshotFrames == 0) {
            asharia::logError("Editor frame debugger smoke recorded unexpected state counts.");
            return false;
        }
        if (runResult.viewportFramesAtFrameDebugPreview !=
            runResult.viewportFramesAtFrameDebugPause) {
            asharia::logError(
                "Editor frame debugger smoke recorded a normal RenderView while previewing.");
            return false;
        }
        if (runResult.inspectedWorldFramesAtFrameDebugPreview !=
            runResult.inspectedWorldFramesAtFrameDebugPause) {
            asharia::logError("Editor frame debugger smoke advanced inspected-world safe points "
                              "while previewing.");
            return false;
        }
        if (runResult.viewportFramesAfterFrameDebugResume <=
            runResult.viewportFramesAtFrameDebugPause) {
            asharia::logError("Editor frame debugger smoke did not resume RenderView recording.");
            return false;
        }
        if (runResult.inspectedWorldFramesAfterFrameDebugResume <=
            runResult.inspectedWorldFramesAtFrameDebugPause) {
            asharia::logError(
                "Editor frame debugger smoke did not resume inspected-world safe points.");
            return false;
        }
        const EditorInspectedWorldSchedulerStats& inspectedWorldStats =
            runResult.inspectedWorldStats;
        if (inspectedWorldStats.frameAdvanceSafePoints == 0 ||
            inspectedWorldStats.gameUpdateSafePoints !=
                inspectedWorldStats.frameAdvanceSafePoints ||
            inspectedWorldStats.scriptUpdateSafePoints !=
                inspectedWorldStats.frameAdvanceSafePoints ||
            inspectedWorldStats.skippedFrameAdvanceSafePoints == 0 ||
            inspectedWorldStats.skippedGameUpdateSafePoints !=
                inspectedWorldStats.skippedFrameAdvanceSafePoints ||
            inspectedWorldStats.skippedScriptUpdateSafePoints !=
                inspectedWorldStats.skippedFrameAdvanceSafePoints) {
            asharia::logError(
                "Editor frame debugger smoke recorded invalid inspected-world safe-point counts.");
            return false;
        }
        const std::optional<EditorFrameDebugCapture>& capture = frameDebugger.latestCapture();
        if (!capture) {
            asharia::logError("Editor frame debugger smoke did not keep a captured snapshot.");
            return false;
        }
        if (capture->diagnostics.renderGraph.passes.size() != 3 ||
            capture->diagnostics.renderGraph.resources.size() != 2 ||
            capture->diagnostics.renderGraph.accessEdges.size() != 4 ||
            capture->diagnostics.renderGraph.dependencyEdges.size() != 2 ||
            capture->diagnostics.renderGraph.transitions.size() != 4) {
            asharia::logError(
                "Editor frame debugger smoke captured unexpected RenderGraph diagnostics: passes " +
                std::to_string(capture->diagnostics.renderGraph.passes.size()) + ", resources " +
                std::to_string(capture->diagnostics.renderGraph.resources.size()) +
                ", access edges " +
                std::to_string(capture->diagnostics.renderGraph.accessEdges.size()) +
                ", dependency edges " +
                std::to_string(capture->diagnostics.renderGraph.dependencyEdges.size()) +
                ", transitions " +
                std::to_string(capture->diagnostics.renderGraph.transitions.size()) + ".");
            return false;
        }
        if (capture->diagnostics.executionEvents.empty()) {
            asharia::logError("Editor frame debugger smoke captured no renderer execution events.");
            return false;
        }
        if (!validateSelectedPreviewEventMapping(runResult, capture->diagnostics)) {
            return false;
        }
        if (!validateStructuralEventReplayUnavailable(*capture)) {
            return false;
        }
        if (!capturedSourceOverlayId(capture->diagnostics.overlay, kEditorSceneGridOverlayId) ||
            capturedSourceOverlayId(capture->diagnostics.overlay,
                                    kEditorSceneTransformGizmoOverlayId) ||
            capturedSourceOverlayId(capture->diagnostics.overlay,
                                    kEditorSceneSelectionOutlineOverlayId)) {
            asharia::logError(
                "Editor frame debugger smoke did not preserve overlay source diagnostics.");
            return false;
        }
        if (!capturedWorldGridSettings(capture->diagnostics.overlay)) {
            asharia::logError(
                "Editor frame debugger smoke did not preserve world-grid diagnostics.");
            return false;
        }
        if (!validateStudioFrameDebugSnapshotProjection(*capture)) {
            return false;
        }
        if (frameDebugger.pausedCapture()) {
            asharia::logError("Editor frame debugger smoke kept a paused capture after resume.");
            return false;
        }
        return true;
    }
} // namespace asharia::editor
