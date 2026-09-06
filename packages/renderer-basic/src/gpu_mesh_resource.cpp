#include "asharia/renderer_basic_vulkan/gpu_mesh_resource.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "asharia/renderer_basic/render_graph_schemas.hpp"
#include "asharia/renderer_basic_vulkan/frame_graph_vulkan.hpp"
#include "asharia/rendergraph/render_graph_builder.hpp"

namespace asharia {
    namespace {
        Error meshError(BasicGpuMeshError code, const std::string& message) {
            return {ErrorDomain::RenderGraph, static_cast<int>(code), "GPU mesh: " + message};
        }
        VoidResult allocateMeshBuffer(const BasicGpuMeshOwnerDesc& desc, VulkanBuffer& target,
                                      std::span<const std::byte> data, VkBufferUsageFlags usage,
                                      VulkanBufferMemoryUsage memory) {
            auto buffer = VulkanBuffer::create({.device = desc.device,
                                                .allocator = desc.allocator,
                                                .size = data.size(),
                                                .usage = usage,
                                                .memoryUsage = memory});
            if (!buffer) {
                return std::unexpected{std::move(buffer.error())};
            }
            if (memory == VulkanBufferMemoryUsage::HostUpload) {
                if (auto written = buffer->upload(data); !written) {
                    return written;
                }
            }
            target = std::move(*buffer);
            return {};
        }
    } // namespace

    BasicGpuMesh::BasicGpuMesh(resource::MeshResourceLease lease, BasicDrawResourceKey key)
        : lease_(std::move(lease)), key_(key) {}

    VoidResult BasicGpuMesh::validate(std::span<const BasicDrawListItem> items,
                                      BasicDrawResourceKey material) const {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            const auto& draw = item.drawItem;
            const bool submesh = std::ranges::any_of(product().submeshes(), [&](const auto& part) {
                return part.firstIndex == draw.firstIndex && part.indexCount == draw.indexCount;
            });
            if (item.context.meshResource != key_ || item.context.meshRevision != revision() ||
                !material || item.context.materialResource != material || draw.vertexCount != 0 ||
                draw.firstVertex != 0 || draw.vertexOffset != 0 || draw.instanceCount == 0 ||
                !submesh || !std::ranges::all_of(item.modelMatrix, [](float value) {
                    return std::isfinite(value);
                })) {
                return std::unexpected{meshError(BasicGpuMeshError::IncompatibleDraw,
                                                 "incompatible submesh/material/revision at draw " +
                                                     std::to_string(i) +
                                                     ", key=" + std::to_string(key_.value) +
                                                     ", revision=" + std::to_string(revision()))};
            }
        }
        return {};
    }

    BasicGpuMeshOwner::BasicGpuMeshOwner(BasicGpuMeshOwnerDesc desc)
        : desc_(desc), thread_(std::this_thread::get_id()) {}

    Result<BasicGpuMeshOwner> BasicGpuMeshOwner::create(BasicGpuMeshOwnerDesc desc) {
        if (desc.device == VK_NULL_HANDLE || desc.allocator == nullptr || !desc.key ||
            desc.key == kBasicValidationMeshResourceKey || desc.maxResidentBytes == 0 ||
            desc.maxResidentVersions == 0) {
            return std::unexpected{
                meshError(BasicGpuMeshError::InvalidInput, "invalid owner descriptor")};
        }
        return BasicGpuMeshOwner{desc};
    }

    VoidResult BasicGpuMeshOwner::requireOwnerThread() const {
        if (thread_ != std::this_thread::get_id()) {
            return std::unexpected{meshError(BasicGpuMeshError::WrongThread, "wrong owner thread")};
        }
        return {};
    }

    BasicGpuMeshStats BasicGpuMeshOwner::stats() const {
        BasicGpuMeshStats result{.uploadsRecorded = uploadsRecorded_,
                                 .published = published_,
                                 .pending = static_cast<bool>(pending_)};
        for (const auto& resident : residents_) {
            if (!resident.mesh.expired()) {
                ++result.residentVersions;
                result.residentBytes += resident.bytes;
                if (!resident.upload.expired()) {
                    result.residentBytes += resident.bytes;
                }
            }
        }
        return result;
    }

    VoidResult BasicGpuMeshOwner::queue(resource::MeshResourceLease lease) {
        if (auto thread = requireOwnerThread(); !thread) {
            return thread;
        }
        if (!lease || lease.product().vertexFormat() != mesh::MeshVertexFormat::P3N3Uv2F32) {
            return std::unexpected{
                meshError(BasicGpuMeshError::InvalidInput, "invalid lease/layout")};
        }
        if (handle_ && *handle_ != lease.handle()) {
            return std::unexpected{
                meshError(BasicGpuMeshError::InvalidInput, "CPU store/slot identity mismatch")};
        }
        if (lease.revision() < highestRevision_) {
            return std::unexpected{meshError(BasicGpuMeshError::StaleRevision,
                                             "stale revision " + std::to_string(lease.revision()))};
        }
        if (lease.revision() == highestRevision_ &&
            ((active_ && active_->revision() == lease.revision()) ||
             (pending_ && pending_->mesh->revision() == lease.revision()))) {
            return {};
        }
        if (std::ranges::any_of(lease.product().materialSlots(), [](const auto& slot) {
                return static_cast<bool>(slot.materialAsset);
            })) {
            return std::unexpected{
                meshError(BasicGpuMeshError::InvalidInput,
                          "fixed unlit material requires unbound product material slots")};
        }
        if (pending_) {
            return std::unexpected{meshError(BasicGpuMeshError::BudgetExceeded,
                                             "one upload is already pending; cancel explicitly")};
        }
        std::erase_if(residents_, [](const auto& entry) { return entry.mesh.expired(); });
        const auto bytes = (lease.product().vertices().size() * sizeof(BasicVertex3D)) +
                           lease.product().indices().size_bytes();
        const auto current = stats();
        if (current.residentVersions >= desc_.maxResidentVersions ||
            bytes > desc_.maxResidentBytes / 2 ||
            current.residentBytes > desc_.maxResidentBytes - bytes * 2) {
            return std::unexpected{meshError(BasicGpuMeshError::BudgetExceeded,
                                             "resident/staging byte or version budget exceeded")};
        }
        std::vector<BasicVertex3D> vertices;
        vertices.reserve(lease.product().vertices().size());
        for (const auto& vertex : lease.product().vertices()) {
            vertices.push_back({.position = {vertex.positionX, vertex.positionY, vertex.positionZ},
                                .color = {0.9F, 0.3F, 0.15F}});
        }
        // Private constructor; move the value into shared ownership without a raw owning pointer.
        auto resource = std::make_shared<BasicGpuMesh>(BasicGpuMesh{std::move(lease), desc_.key});
        auto upload = std::make_shared<Upload>();
        upload->mesh = resource;
        resource->device_ = desc_.device;
        const auto vertexBytes = std::as_bytes(std::span{vertices});
        const auto indexBytes = std::as_bytes(resource->product().indices());
        if (auto result = allocateMeshBuffer(desc_, upload->vertexStaging, vertexBytes,
                                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                             VulkanBufferMemoryUsage::HostUpload);
            !result) {
            return result;
        }
        if (auto result = allocateMeshBuffer(desc_, upload->indexStaging, indexBytes,
                                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                             VulkanBufferMemoryUsage::HostUpload);
            !result) {
            return result;
        }
        if (auto result = allocateMeshBuffer(desc_, resource->vertices_, vertexBytes,
                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                             VulkanBufferMemoryUsage::DeviceLocal);
            !result) {
            return result;
        }
        if (auto result = allocateMeshBuffer(desc_, resource->indices_, indexBytes,
                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                             VulkanBufferMemoryUsage::DeviceLocal);
            !result) {
            return result;
        }
        handle_ = resource->lease_.handle();
        highestRevision_ = resource->revision();
        residents_.push_back({.mesh = resource, .upload = upload, .bytes = bytes});
        pending_ = std::move(upload);
        return {};
    }

    VoidResult BasicGpuMeshOwner::recordUpload(const VulkanFrameRecordContext& frame,
                                               RenderGraphDiagnosticsSnapshot* diagnostics) {
        if (auto thread = requireOwnerThread(); !thread) {
            return thread;
        }
        if (!pending_ || pending_->recorded ||
            (frame.frameLoop == nullptr && frame.submission == nullptr) ||
            frame.commandBuffer == VK_NULL_HANDLE) {
            return std::unexpected{
                meshError(BasicGpuMeshError::InvalidSubmission,
                          "upload requires an unrecorded candidate and submission owner")};
        }
        RenderGraph graph;
        std::vector<VulkanRenderGraphBufferBinding> bindings;
        const auto addCopy = [&](const std::string& name, const VulkanBuffer& staging,
                                 const VulkanBuffer& device, RenderGraphBufferState finalState) {
            auto source = graph.importBuffer({.name = name + "Staging",
                                              .byteSize = staging.size(),
                                              .initialState = RenderGraphBufferState::TransferRead,
                                              .finalState = RenderGraphBufferState::TransferRead});
            auto target = graph.importBuffer({.name = name,
                                              .byteSize = device.size(),
                                              .initialState = RenderGraphBufferState::Undefined,
                                              .finalState = finalState});
            bindings.push_back(
                {.buffer = source, .vulkanBuffer = staging.handle(), .size = staging.size()});
            bindings.push_back(
                {.buffer = target, .vulkanBuffer = device.handle(), .size = device.size()});
            graph.addPass(name + "Upload", kBasicTransferCopyBufferPassType)
                .readTransferBuffer("source", source)
                .writeBuffer("target", target)
                .recordCommands([](RenderGraphCommandList& commands) {
                    commands.copyBuffer("source", "target");
                })
                .execute([&frame, &bindings](RenderGraphPassContext pass) -> VoidResult {
                    if (auto barriers = recordRenderGraphBufferTransitions(
                            frame, pass.bufferTransitionsBefore, bindings);
                        !barriers) {
                        return barriers;
                    }
                    auto src = findVulkanRenderGraphBufferTransferRead(pass, "source", bindings);
                    if (!src) {
                        return std::unexpected{std::move(src.error())};
                    }
                    auto dst = findVulkanRenderGraphBufferTransferWrite(pass, "target", bindings);
                    if (!dst) {
                        return std::unexpected{std::move(dst.error())};
                    }
                    const VkBufferCopy copy{
                        .srcOffset = src->offset, .dstOffset = dst->offset, .size = src->size};
                    vkCmdCopyBuffer(frame.commandBuffer, src->vulkanBuffer, dst->vulkanBuffer, 1,
                                    &copy);
                    return {};
                });
        };
        addCopy("MeshVertices", pending_->vertexStaging, pending_->mesh->vertices_,
                RenderGraphBufferState::VertexRead);
        addCopy("MeshIndices", pending_->indexStaging, pending_->mesh->indices_,
                RenderGraphBufferState::IndexRead);
        auto compiled = graph.compile(basicRenderGraphSchemaRegistry());
        if (!compiled) {
            return std::unexpected{std::move(compiled.error())};
        }
        // Pin before recording any command, including on partial-record failure.
        if (!frame.deferDeletion([keep = pending_]() { static_cast<void>(keep); })) {
            return std::unexpected{meshError(BasicGpuMeshError::InvalidSubmission,
                                             "cannot retain upload through GPU completion")};
        }
        pending_->recorded = true;
        pending_->frameLoop = frame.frameLoop;
        if (frame.frameLoop != nullptr) {
            pending_->epoch = frame.frameLoop->submittedFrameEpoch() + 1;
        } else {
            pending_->submission = frame.submission->receipt();
        }
        if (auto execute = graph.execute(*compiled); !execute) {
            return execute;
        }
        if (auto barriers = recordRenderGraphBufferTransitions(
                frame, compiled->finalBufferTransitions, bindings);
            !barriers) {
            return barriers;
        }
        if (diagnostics != nullptr) {
            *diagnostics = graph.diagnosticsSnapshot(*compiled);
        }
        pending_->recordingSucceeded = true;
        ++uploadsRecorded_;
        return {};
    }

    VoidResult BasicGpuMeshOwner::confirmUploadSubmission(const VulkanFrameLoop& frameLoop) {
        if (auto thread = requireOwnerThread(); !thread) {
            return thread;
        }
        if (!pending_ || !pending_->recordingSucceeded || pending_->confirmed ||
            pending_->frameLoop != &frameLoop ||
            pending_->epoch != frameLoop.submittedFrameEpoch()) {
            return std::unexpected{meshError(BasicGpuMeshError::InvalidSubmission,
                                             "upload was not submitted in its recorded frame")};
        }
        pending_->confirmed = true;
        return {};
    }

    VoidResult BasicGpuMeshOwner::publishCompleted(const VulkanFrameLoop& frameLoop) {
        if (auto thread = requireOwnerThread(); !thread) {
            return thread;
        }
        if (!pending_ || !pending_->confirmed || pending_->frameLoop != &frameLoop ||
            frameLoop.completedFrameEpoch() < pending_->epoch) {
            return std::unexpected{
                meshError(BasicGpuMeshError::NotReady, "upload has no confirmed GPU completion")};
        }
        active_ = pending_->mesh;
        pending_.reset();
        ++published_;
        return {};
    }

    VoidResult BasicGpuMeshOwner::confirmUploadSubmission() {
        if (auto thread = requireOwnerThread(); !thread) {
            return thread;
        }
        if (!pending_ || pending_->frameLoop != nullptr || !pending_->recordingSucceeded ||
            pending_->confirmed || !pending_->submission.submitted()) {
            return std::unexpected{meshError(BasicGpuMeshError::InvalidSubmission,
                                             "upload has no matching successful host submission")};
        }
        pending_->confirmed = true;
        return {};
    }
    VoidResult BasicGpuMeshOwner::publishCompleted() {
        if (auto thread = requireOwnerThread(); !thread) {
            return thread;
        }
        if (!pending_ || pending_->frameLoop != nullptr || !pending_->confirmed ||
            !pending_->submission.completed()) {
            return std::unexpected{meshError(BasicGpuMeshError::NotReady,
                                             "upload has no observed host fence completion")};
        }
        active_ = pending_->mesh;
        pending_.reset();
        ++published_;
        return {};
    }

    Result<std::shared_ptr<const BasicGpuMesh>> BasicGpuMeshOwner::acquire() const {
        if (auto thread = requireOwnerThread(); !thread) {
            return std::unexpected{std::move(thread.error())};
        }
        if (!active_) {
            return std::unexpected{meshError(BasicGpuMeshError::NotReady, "no active resource")};
        }
        return active_;
    }
    VoidResult BasicGpuMeshOwner::cancelUpload() {
        if (auto thread = requireOwnerThread(); !thread) {
            return thread;
        }
        pending_.reset();
        return {};
    }
    VoidResult BasicGpuMeshOwner::clear() {
        if (auto thread = requireOwnerThread(); !thread) {
            return thread;
        }
        pending_.reset();
        active_.reset();
        return {};
    }
} // namespace asharia
