#include "render_graph_debug_summary_tables.hpp"

#include <cstddef>
#include <span>
#include <string>

#include "asharia/rendergraph/render_graph_compile.hpp"

#include "render_graph_debug_names.hpp"
#include "render_graph_declaration_view.hpp"

namespace asharia::rendergraph_internal {

    namespace {

        [[nodiscard]] inline std::string
        imageHandleLabel(const RenderGraphDeclarationView& declarations,
                         RenderGraphImageHandle image) {
            std::string label = "#";
            label += std::to_string(image.index);
            if (image.index < declarations.images.size() &&
                !declarations.images[image.index].name.empty()) {
                label += " ";
                label += declarations.images[image.index].name;
            }
            return label;
        }

        [[nodiscard]] inline std::string
        bufferHandleLabel(const RenderGraphDeclarationView& declarations,
                          RenderGraphBufferHandle buffer) {
            std::string label = "#";
            label += std::to_string(buffer.index);
            if (buffer.index < declarations.buffers.size() &&
                !declarations.buffers[buffer.index].name.empty()) {
                label += " ";
                label += declarations.buffers[buffer.index].name;
            }
            return label;
        }

        [[nodiscard]] inline std::string
        dependencyResourceLabel(const RenderGraphDeclarationView& declarations,
                                const RenderGraphPassDependency& dependency) {
            if (dependency.resourceKind == RenderGraphResourceKind::Buffer) {
                return bufferHandleLabel(declarations, dependency.buffer);
            }
            return imageHandleLabel(declarations, dependency.image);
        }

        [[nodiscard]] inline std::string
        passDeclarationLabel(const RenderGraphDeclarationView& declarations,
                             std::size_t passIndex) {
            std::string label = "#";
            label += std::to_string(passIndex);
            if (passIndex < declarations.passes.size() &&
                !declarations.passes[passIndex].name.empty()) {
                label += " ";
                label += declarations.passes[passIndex].name;
            }
            return label;
        }

        [[nodiscard]] inline std::string
        slotImageLabel(const RenderGraphDeclarationView& declarations,
                       const RenderGraphImageSlot& slot) {
            std::string label = slot.name;
            if (slot.shaderStage != RenderGraphShaderStage::None) {
                label += "(";
                label += shaderStageName(slot.shaderStage);
                label += ")";
            }
            label += "=";
            label += imageHandleLabel(declarations, slot.image);
            return label;
        }

        [[nodiscard]] inline std::string
        slotBufferLabel(const RenderGraphDeclarationView& declarations,
                        const RenderGraphBufferSlot& slot) {
            std::string label = slot.name;
            if (slot.shaderStage != RenderGraphShaderStage::None) {
                label += "(";
                label += shaderStageName(slot.shaderStage);
                label += ")";
            }
            label += "=";
            label += bufferHandleLabel(declarations, slot.buffer);
            return label;
        }

        [[nodiscard]] inline std::string
        imageSlotList(const RenderGraphDeclarationView& declarations,
                      std::span<const RenderGraphImageSlot> slots) {
            if (slots.empty()) {
                return "-";
            }

            std::string labels;
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (index > 0) {
                    labels += ", ";
                }
                labels += slotImageLabel(declarations, slots[index]);
            }
            return labels;
        }

        [[nodiscard]] inline std::string
        bufferSlotList(const RenderGraphDeclarationView& declarations,
                       std::span<const RenderGraphBufferSlot> slots) {
            if (slots.empty()) {
                return "-";
            }

            std::string labels;
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (index > 0) {
                    labels += ", ";
                }
                labels += slotBufferLabel(declarations, slots[index]);
            }
            return labels;
        }

    } // namespace

    void appendResourceTables(const RenderGraphDeclarationView& declarations, std::string& output) {
        output += "### RenderGraph Resources\n\n";
        output += "| # | Name | Lifetime | Format | Extent | Initial | Final |\n";
        output += "|---:|---|---|---|---:|---|---|\n";
        for (std::size_t index = 0; index < declarations.images.size(); ++index) {
            const RenderGraphImageDesc& image = declarations.images[index];
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
        for (std::size_t index = 0; index < declarations.buffers.size(); ++index) {
            const RenderGraphBufferDesc& buffer = declarations.buffers[index];
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
    }

    void appendPassTable(const RenderGraphDeclarationView& declarations,
                         const RenderGraphCompileResult& compiled, std::string& output) {
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
            output += imageSlotList(declarations, pass.colorWriteSlots);
            output += " | ";
            output += imageSlotList(declarations, pass.shaderReadSlots);
            output += " | ";
            output += imageSlotList(declarations, pass.depthReadSlots);
            output += " | ";
            output += imageSlotList(declarations, pass.depthWriteSlots);
            output += " | ";
            output += imageSlotList(declarations, pass.depthSampledReadSlots);
            output += " | ";
            output += imageSlotList(declarations, pass.transferReadSlots);
            output += " | ";
            output += imageSlotList(declarations, pass.transferWriteSlots);
            output += " | ";
            output += bufferSlotList(declarations, pass.bufferReadSlots);
            output += " | ";
            output += bufferSlotList(declarations, pass.bufferTransferReadSlots);
            output += " | ";
            output += bufferSlotList(declarations, pass.bufferWriteSlots);
            output += " | ";
            output += bufferSlotList(declarations, pass.bufferStorageReadWriteSlots);
            output += " |\n";
        }
    }

    void appendDependencyTable(const RenderGraphDeclarationView& declarations,
                               const RenderGraphCompileResult& compiled, std::string& output) {
        output += "\n### RenderGraph Dependencies\n\n";
        output += "| From | To | Resource | Reason |\n";
        output += "|---|---|---|---|\n";
        for (const RenderGraphPassDependency& dependency : compiled.dependencies) {
            output += "| ";
            output += passDeclarationLabel(declarations, dependency.fromDeclarationIndex);
            output += " | ";
            output += passDeclarationLabel(declarations, dependency.toDeclarationIndex);
            output += " | ";
            output += dependencyResourceLabel(declarations, dependency);
            output += " | ";
            output += dependency.reason;
            output += " |\n";
        }
    }

    void appendCulledPassTable(const RenderGraphCompileResult& compiled, std::string& output) {
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
    }

} // namespace asharia::rendergraph_internal
