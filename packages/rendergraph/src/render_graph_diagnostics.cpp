#include <cstddef>
#include <span>
#include <string>

#include "render_graph_internal.hpp"

namespace asharia {

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    RenderGraphDiagnosticsSnapshot
    RenderGraph::Impl::diagnosticsSnapshot(const RenderGraphCompileResult& compiled) const {
        RenderGraphDiagnosticsSnapshot snapshot;
        snapshot.declaredPassCount = compiled.declaredPassCount;
        snapshot.declaredImageCount = compiled.declaredImageCount;
        snapshot.declaredBufferCount = compiled.declaredBufferCount;
        snapshot.passes.reserve(compiled.passes.size());
        snapshot.resources.reserve(images_.size() + buffers_.size());
        snapshot.dependencyEdges.reserve(compiled.dependencies.size());
        snapshot.transitions.reserve(compiled.finalTransitions.size() +
                                     compiled.finalBufferTransitions.size());
        snapshot.culledPasses = compiled.culledPasses;
        snapshot.transientImages = compiled.transientImages;
        snapshot.transientBuffers = compiled.transientBuffers;

        for (std::size_t imageIndex = 0; imageIndex < images_.size(); ++imageIndex) {
            const RenderGraphImageDesc& image = images_[imageIndex];
            snapshot.resources.push_back(RenderGraphDiagnosticsResourceNode{
                .kind = RenderGraphResourceKind::Image,
                .resourceIndex = static_cast<std::uint32_t>(imageIndex),
                .name = image.name,
                .imageLifetime = image.lifetime,
                .imageFormat = image.format,
                .imageExtent = image.extent,
                .imageInitialAccess =
                    RenderGraphImageAccess{
                        .state = image.initialState,
                        .shaderStage = image.initialShaderStage,
                    },
                .imageFinalAccess =
                    RenderGraphImageAccess{
                        .state = image.finalState,
                        .shaderStage = image.finalShaderStage,
                    },
            });
        }

        for (std::size_t bufferIndex = 0; bufferIndex < buffers_.size(); ++bufferIndex) {
            const RenderGraphBufferDesc& buffer = buffers_[bufferIndex];
            snapshot.resources.push_back(RenderGraphDiagnosticsResourceNode{
                .kind = RenderGraphResourceKind::Buffer,
                .resourceIndex = static_cast<std::uint32_t>(bufferIndex),
                .name = buffer.name,
                .bufferLifetime = buffer.lifetime,
                .bufferByteSize = buffer.byteSize,
                .bufferInitialAccess =
                    RenderGraphBufferAccess{
                        .state = buffer.initialState,
                        .shaderStage = buffer.initialShaderStage,
                    },
                .bufferFinalAccess =
                    RenderGraphBufferAccess{
                        .state = buffer.finalState,
                        .shaderStage = buffer.finalShaderStage,
                    },
            });
        }

        const auto imageName = [this](RenderGraphImageHandle image) -> std::string {
            if (image.index < images_.size()) {
                return images_[image.index].name;
            }
            return {};
        };
        const auto bufferName = [this](RenderGraphBufferHandle buffer) -> std::string {
            if (buffer.index < buffers_.size()) {
                return buffers_[buffer.index].name;
            }
            return {};
        };
        const auto compiledPassIndex = [&compiled](std::size_t declarationIndex) -> std::size_t {
            for (std::size_t passIndex = 0; passIndex < compiled.passes.size(); ++passIndex) {
                if (compiled.passes[passIndex].declarationIndex == declarationIndex) {
                    return passIndex;
                }
            }
            return compiled.passes.size();
        };
        const auto appendImageEdges =
            [&imageName, &snapshot](std::size_t passIndex, const RenderGraphCompiledPass& pass,
                                    RenderGraphSlotAccess access,
                                    std::span<const RenderGraphImageSlot> slots) {
                for (const RenderGraphImageSlot& slot : slots) {
                    snapshot.accessEdges.push_back(RenderGraphDiagnosticsAccessEdge{
                        .passIndex = passIndex,
                        .declarationIndex = pass.declarationIndex,
                        .passName = pass.name,
                        .resourceKind = RenderGraphResourceKind::Image,
                        .resourceIndex = slot.image.index,
                        .resourceName = imageName(slot.image),
                        .slotName = slot.name,
                        .access = access,
                        .shaderStage = slot.shaderStage,
                    });
                }
            };
        const auto appendBufferEdges =
            [&bufferName, &snapshot](std::size_t passIndex, const RenderGraphCompiledPass& pass,
                                     RenderGraphSlotAccess access,
                                     std::span<const RenderGraphBufferSlot> slots) {
                for (const RenderGraphBufferSlot& slot : slots) {
                    snapshot.accessEdges.push_back(RenderGraphDiagnosticsAccessEdge{
                        .passIndex = passIndex,
                        .declarationIndex = pass.declarationIndex,
                        .passName = pass.name,
                        .resourceKind = RenderGraphResourceKind::Buffer,
                        .resourceIndex = slot.buffer.index,
                        .resourceName = bufferName(slot.buffer),
                        .slotName = slot.name,
                        .access = access,
                        .shaderStage = slot.shaderStage,
                    });
                }
            };

        for (std::size_t passIndex = 0; passIndex < compiled.passes.size(); ++passIndex) {
            const RenderGraphCompiledPass& pass = compiled.passes[passIndex];
            snapshot.passes.push_back(RenderGraphDiagnosticsPassNode{
                .passIndex = passIndex,
                .declarationIndex = pass.declarationIndex,
                .name = pass.name,
                .type = pass.type,
                .paramsType = pass.paramsType,
                .allowCulling = pass.allowCulling,
                .hasSideEffects = pass.hasSideEffects,
                .commandCount = pass.commands.size(),
                .imageTransitionCount = pass.transitionsBefore.size(),
                .bufferTransitionCount = pass.bufferTransitionsBefore.size(),
            });

            for (std::size_t commandIndex = 0; commandIndex < pass.commands.size();
                 ++commandIndex) {
                const RenderGraphCommand& command = pass.commands[commandIndex];
                snapshot.commands.push_back(RenderGraphDiagnosticsCommandNode{
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .commandIndex = commandIndex,
                    .passName = pass.name,
                    .kind = command.kind,
                    .detail = commandDetail(command),
                });
            }

            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::ColorWrite,
                             pass.colorWriteSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::ShaderRead,
                             pass.shaderReadSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::DepthAttachmentRead,
                             pass.depthReadSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::DepthAttachmentWrite,
                             pass.depthWriteSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::DepthSampledRead,
                             pass.depthSampledReadSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::TransferRead,
                             pass.transferReadSlots);
            appendImageEdges(passIndex, pass, RenderGraphSlotAccess::TransferWrite,
                             pass.transferWriteSlots);
            appendBufferEdges(passIndex, pass, RenderGraphSlotAccess::BufferShaderRead,
                              pass.bufferReadSlots);
            appendBufferEdges(passIndex, pass, RenderGraphSlotAccess::BufferTransferRead,
                              pass.bufferTransferReadSlots);
            appendBufferEdges(passIndex, pass, RenderGraphSlotAccess::BufferTransferWrite,
                              pass.bufferWriteSlots);
            appendBufferEdges(passIndex, pass, RenderGraphSlotAccess::BufferStorageReadWrite,
                              pass.bufferStorageReadWriteSlots);

            for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                    .phase = RenderGraphDiagnosticsTransitionPhase::BeforePass,
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .passName = pass.name,
                    .resourceKind = RenderGraphResourceKind::Image,
                    .resourceIndex = transition.image.index,
                    .resourceName = transition.imageName,
                    .oldImageAccess =
                        RenderGraphImageAccess{
                            .state = transition.oldState,
                            .shaderStage = transition.oldShaderStage,
                        },
                    .newImageAccess =
                        RenderGraphImageAccess{
                            .state = transition.newState,
                            .shaderStage = transition.newShaderStage,
                        },
                });
            }
            for (const RenderGraphBufferTransition& transition : pass.bufferTransitionsBefore) {
                snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                    .phase = RenderGraphDiagnosticsTransitionPhase::BeforePass,
                    .passIndex = passIndex,
                    .declarationIndex = pass.declarationIndex,
                    .passName = pass.name,
                    .resourceKind = RenderGraphResourceKind::Buffer,
                    .resourceIndex = transition.buffer.index,
                    .resourceName = transition.bufferName,
                    .oldBufferAccess =
                        RenderGraphBufferAccess{
                            .state = transition.oldState,
                            .shaderStage = transition.oldShaderStage,
                        },
                    .newBufferAccess =
                        RenderGraphBufferAccess{
                            .state = transition.newState,
                            .shaderStage = transition.newShaderStage,
                        },
                });
            }
        }

        for (const RenderGraphPassDependency& dependency : compiled.dependencies) {
            snapshot.dependencyEdges.push_back(RenderGraphDiagnosticsDependencyEdge{
                .fromPassIndex = compiledPassIndex(dependency.fromDeclarationIndex),
                .toPassIndex = compiledPassIndex(dependency.toDeclarationIndex),
                .fromDeclarationIndex = dependency.fromDeclarationIndex,
                .toDeclarationIndex = dependency.toDeclarationIndex,
                .resourceKind = dependency.resourceKind,
                .resourceIndex = dependency.resourceKind == RenderGraphResourceKind::Buffer
                                     ? dependency.buffer.index
                                     : dependency.image.index,
                .resourceName = dependency.resourceKind == RenderGraphResourceKind::Buffer
                                    ? dependency.bufferName
                                    : dependency.imageName,
                .reason = dependency.reason,
            });
        }

        for (const RenderGraphImageTransition& transition : compiled.finalTransitions) {
            snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                .phase = RenderGraphDiagnosticsTransitionPhase::Final,
                .passIndex = compiled.passes.size(),
                .declarationIndex = compiled.declaredPassCount,
                .passName = {},
                .resourceKind = RenderGraphResourceKind::Image,
                .resourceIndex = transition.image.index,
                .resourceName = transition.imageName,
                .oldImageAccess =
                    RenderGraphImageAccess{
                        .state = transition.oldState,
                        .shaderStage = transition.oldShaderStage,
                    },
                .newImageAccess =
                    RenderGraphImageAccess{
                        .state = transition.newState,
                        .shaderStage = transition.newShaderStage,
                    },
            });
        }
        for (const RenderGraphBufferTransition& transition : compiled.finalBufferTransitions) {
            snapshot.transitions.push_back(RenderGraphDiagnosticsTransition{
                .phase = RenderGraphDiagnosticsTransitionPhase::Final,
                .passIndex = compiled.passes.size(),
                .declarationIndex = compiled.declaredPassCount,
                .passName = {},
                .resourceKind = RenderGraphResourceKind::Buffer,
                .resourceIndex = transition.buffer.index,
                .resourceName = transition.bufferName,
                .oldBufferAccess =
                    RenderGraphBufferAccess{
                        .state = transition.oldState,
                        .shaderStage = transition.oldShaderStage,
                    },
                .newBufferAccess =
                    RenderGraphBufferAccess{
                        .state = transition.newState,
                        .shaderStage = transition.newShaderStage,
                    },
            });
        }

        return snapshot;
    }
    // NOLINTEND(readability-function-cognitive-complexity)

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    std::string
    RenderGraph::Impl::formatDebugTables(const RenderGraphCompileResult& compiled) const {
        std::string output;
        output += "### RenderGraph Resources\n\n";
        output += "| # | Name | Lifetime | Format | Extent | Initial | Final |\n";
        output += "|---:|---|---|---|---:|---|---|\n";
        for (std::size_t index = 0; index < images_.size(); ++index) {
            const RenderGraphImageDesc& image = images_[index];
            output += "| ";
            output += std::to_string(index);
            output += " | ";
            output += image.name;
            output += " | ";
            output += imageLifetimeName(image.lifetime);
            output += " | ";
            output += imageFormatName(image.format);
            output += " | ";
            output += std::to_string(image.extent.width);
            output += "x";
            output += std::to_string(image.extent.height);
            output += " | ";
            output += imageAccessName(image.initialState, image.initialShaderStage);
            output += " | ";
            output += imageAccessName(image.finalState, image.finalShaderStage);
            output += " |\n";
        }

        output += "\n### RenderGraph Buffers\n\n";
        output += "| # | Name | Lifetime | Bytes | Initial | Final |\n";
        output += "|---:|---|---|---:|---|---|\n";
        for (std::size_t index = 0; index < buffers_.size(); ++index) {
            const RenderGraphBufferDesc& buffer = buffers_[index];
            output += "| ";
            output += std::to_string(index);
            output += " | ";
            output += buffer.name;
            output += " | ";
            output += bufferLifetimeName(buffer.lifetime);
            output += " | ";
            output += std::to_string(buffer.byteSize);
            output += " | ";
            output += bufferAccessName(buffer.initialState, buffer.initialShaderStage);
            output += " | ";
            output += bufferAccessName(buffer.finalState, buffer.finalShaderStage);
            output += " |\n";
        }

        output += "\n### RenderGraph Passes\n\n";
        output += "| # | Decl # | Name | Type | Params | Cullable | Side Effects | "
                  "Before Transitions | Buffer Transitions | Color Writes | Shader Reads | "
                  "Depth Reads | Depth Writes | Depth Sampled Reads | Transfer Reads | "
                  "Transfer Writes | Buffer Reads | Buffer Transfer Reads | Buffer Writes | "
                  "Buffer Storage Read/Writes |\n";
        output += "|---:|---:|---|---|---|---|---|---:|---:|---|---|---|---|---|---|---|---|---"
                  "|---|\n";
        for (std::size_t index = 0; index < compiled.passes.size(); ++index) {
            const RenderGraphCompiledPass& pass = compiled.passes[index];
            output += "| ";
            output += std::to_string(index);
            output += " | ";
            output += std::to_string(pass.declarationIndex);
            output += " | ";
            output += pass.name;
            output += " | ";
            output += pass.type.empty() ? "-" : pass.type;
            output += " | ";
            output += pass.paramsType.empty() ? "-" : pass.paramsType;
            output += " | ";
            output += pass.allowCulling ? "yes" : "no";
            output += " | ";
            output += pass.hasSideEffects ? "yes" : "no";
            output += " | ";
            output += std::to_string(pass.transitionsBefore.size());
            output += " | ";
            output += std::to_string(pass.bufferTransitionsBefore.size());
            output += " | ";
            output += imageSlotList(pass.colorWriteSlots);
            output += " | ";
            output += imageSlotList(pass.shaderReadSlots);
            output += " | ";
            output += imageSlotList(pass.depthReadSlots);
            output += " | ";
            output += imageSlotList(pass.depthWriteSlots);
            output += " | ";
            output += imageSlotList(pass.depthSampledReadSlots);
            output += " | ";
            output += imageSlotList(pass.transferReadSlots);
            output += " | ";
            output += imageSlotList(pass.transferWriteSlots);
            output += " | ";
            output += bufferSlotList(pass.bufferReadSlots);
            output += " | ";
            output += bufferSlotList(pass.bufferTransferReadSlots);
            output += " | ";
            output += bufferSlotList(pass.bufferWriteSlots);
            output += " | ";
            output += bufferSlotList(pass.bufferStorageReadWriteSlots);
            output += " |\n";
        }

        output += "\n### RenderGraph Dependencies\n\n";
        output += "| From | To | Resource | Reason |\n";
        output += "|---|---|---|---|\n";
        for (const RenderGraphPassDependency& dependency : compiled.dependencies) {
            output += "| ";
            output += passDeclarationLabel(dependency.fromDeclarationIndex);
            output += " | ";
            output += passDeclarationLabel(dependency.toDeclarationIndex);
            output += " | ";
            output += dependencyResourceLabel(dependency);
            output += " | ";
            output += dependency.reason;
            output += " |\n";
        }

        output += "\n### RenderGraph Culled Passes\n\n";
        output += "| Decl # | Name | Type | Reason |\n";
        output += "|---:|---|---|---|\n";
        for (const RenderGraphCulledPass& pass : compiled.culledPasses) {
            output += "| ";
            output += std::to_string(pass.declarationIndex);
            output += " | ";
            output += pass.name;
            output += " | ";
            output += pass.type.empty() ? "-" : pass.type;
            output += " | ";
            output += pass.reason;
            output += " |\n";
        }

        output += "\n### RenderGraph Slots\n\n";
        output += "| Pass | Access | Slot | Resource |\n";
        output += "|---|---|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            appendSlotRows(output, pass.name, "ColorWrite", pass.colorWriteSlots);
            appendSlotRows(output, pass.name, "ShaderRead", pass.shaderReadSlots);
            appendSlotRows(output, pass.name, "DepthAttachmentRead", pass.depthReadSlots);
            appendSlotRows(output, pass.name, "DepthAttachmentWrite", pass.depthWriteSlots);
            appendSlotRows(output, pass.name, "DepthSampledRead", pass.depthSampledReadSlots);
            appendSlotRows(output, pass.name, "TransferRead", pass.transferReadSlots);
            appendSlotRows(output, pass.name, "TransferWrite", pass.transferWriteSlots);
            appendBufferSlotRows(output, pass.name, "BufferShaderRead", pass.bufferReadSlots);
            appendBufferSlotRows(output, pass.name, "BufferTransferRead",
                                 pass.bufferTransferReadSlots);
            appendBufferSlotRows(output, pass.name, "BufferTransferWrite", pass.bufferWriteSlots);
            appendBufferSlotRows(output, pass.name, "BufferStorageReadWrite",
                                 pass.bufferStorageReadWriteSlots);
        }

        output += "\n### RenderGraph Commands\n\n";
        output += "| Pass | # | Command | Detail |\n";
        output += "|---|---:|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            appendCommandRows(output, pass);
        }

        output += "\n### RenderGraph Transitions\n\n";
        output += "| Phase | Pass | Resource | Old State | New State |\n";
        output += "|---|---|---|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                appendTransitionRow(output, "Before", pass.name, transition);
            }
            for (const RenderGraphBufferTransition& transition : pass.bufferTransitionsBefore) {
                appendTransitionRow(output, "Before", pass.name, transition);
            }
        }
        for (const RenderGraphImageTransition& transition : compiled.finalTransitions) {
            appendTransitionRow(output, "Final", "-", transition);
        }
        for (const RenderGraphBufferTransition& transition : compiled.finalBufferTransitions) {
            appendTransitionRow(output, "Final", "-", transition);
        }

        output += "\n### RenderGraph Transients\n\n";
        output += "| Image | Format | Extent | First Pass | Last Pass | Final Access |\n";
        output += "|---|---|---:|---:|---:|---|\n";
        for (const RenderGraphTransientImageAllocation& transient : compiled.transientImages) {
            output += "| ";
            output += imageHandleLabel(transient.image);
            output += " | ";
            output += imageFormatName(transient.format);
            output += " | ";
            output += std::to_string(transient.extent.width);
            output += "x";
            output += std::to_string(transient.extent.height);
            output += " | ";
            output += std::to_string(transient.firstPassIndex);
            output += " | ";
            output += std::to_string(transient.lastPassIndex);
            output += " | ";
            output += imageAccessName(transient.finalState, transient.finalShaderStage);
            output += " |\n";
        }

        output += "\n### RenderGraph Transient Buffers\n\n";
        output += "| Buffer | Bytes | First Pass | Last Pass | Final Access |\n";
        output += "|---|---:|---:|---:|---|\n";
        for (const RenderGraphTransientBufferAllocation& transient : compiled.transientBuffers) {
            output += "| ";
            output += bufferHandleLabel(transient.buffer);
            output += " | ";
            output += std::to_string(transient.byteSize);
            output += " | ";
            output += std::to_string(transient.firstPassIndex);
            output += " | ";
            output += std::to_string(transient.lastPassIndex);
            output += " | ";
            output += bufferAccessName(transient.finalState, transient.finalShaderStage);
            output += " |\n";
        }

        return output;
    }
    // NOLINTEND(readability-function-cognitive-complexity)

} // namespace asharia
