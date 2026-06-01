#include "render_graph_debug_detail_tables.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

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

        inline void appendSlotRows(const RenderGraphDeclarationView& declarations,
                                   std::string& output, std::string_view passName,
                                   std::string_view access,
                                   std::span<const RenderGraphImageSlot> slots) {
            if (slots.empty()) {
                return;
            }

            for (const RenderGraphImageSlot& slot : slots) {
                output += "| ";
                output += passName;
                output += " | ";
                output += access;
                output += " | ";
                output += slot.name;
                if (slot.shaderStage != RenderGraphShaderStage::None) {
                    output += "(";
                    output += shaderStageName(slot.shaderStage);
                    output += ")";
                }
                output += " | ";
                output += imageHandleLabel(declarations, slot.image);
                output += " |\n";
            }
        }

        inline void appendBufferSlotRows(const RenderGraphDeclarationView& declarations,
                                         std::string& output, std::string_view passName,
                                         std::string_view access,
                                         std::span<const RenderGraphBufferSlot> slots) {
            if (slots.empty()) {
                return;
            }

            for (const RenderGraphBufferSlot& slot : slots) {
                output += "| ";
                output += passName;
                output += " | ";
                output += access;
                output += " | ";
                output += slot.name;
                if (slot.shaderStage != RenderGraphShaderStage::None) {
                    output += "(";
                    output += shaderStageName(slot.shaderStage);
                    output += ")";
                }
                output += " | ";
                output += bufferHandleLabel(declarations, slot.buffer);
                output += " |\n";
            }
        }

        inline void appendCommandRows(std::string& output, const RenderGraphCompiledPass& pass) {
            for (std::size_t index = 0; index < pass.commands.size(); ++index) {
                const RenderGraphCommand& command = pass.commands[index];
                output += "| ";
                output += pass.name;
                output += " | ";
                output += std::to_string(index);
                output += " | ";
                output += commandKindName(command.kind);
                output += " | ";
                output += commandDetail(command);
                output += " |\n";
            }
        }

        inline void appendTransitionRow(const RenderGraphDeclarationView& declarations,
                                        std::string& output, std::string_view phase,
                                        std::string_view passName,
                                        const RenderGraphImageTransition& transition) {
            output += "| ";
            output += phase;
            output += " | ";
            output += passName;
            output += " | ";
            output += imageHandleLabel(declarations, transition.image);
            output += " | ";
            output += imageAccessName(transition.oldState, transition.oldShaderStage);
            output += " | ";
            output += imageAccessName(transition.newState, transition.newShaderStage);
            output += " |\n";
        }

        inline void appendTransitionRow(const RenderGraphDeclarationView& declarations,
                                        std::string& output, std::string_view phase,
                                        std::string_view passName,
                                        const RenderGraphBufferTransition& transition) {
            output += "| ";
            output += phase;
            output += " | ";
            output += passName;
            output += " | ";
            output += bufferHandleLabel(declarations, transition.buffer);
            output += " | ";
            output += bufferAccessName(transition.oldState, transition.oldShaderStage);
            output += " | ";
            output += bufferAccessName(transition.newState, transition.newShaderStage);
            output += " |\n";
        }

    } // namespace

    void appendSlotTable(const RenderGraphDeclarationView& declarations,
                         const RenderGraphCompileResult& compiled, std::string& output) {
        output += "\n### RenderGraph Slots\n\n";
        output += "| Pass | Access | Slot | Resource |\n";
        output += "|---|---|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            appendSlotRows(declarations, output, pass.name, "ColorWrite", pass.colorWriteSlots);
            appendSlotRows(declarations, output, pass.name, "ShaderRead", pass.shaderReadSlots);
            appendSlotRows(declarations, output, pass.name, "DepthAttachmentRead",
                           pass.depthReadSlots);
            appendSlotRows(declarations, output, pass.name, "DepthAttachmentWrite",
                           pass.depthWriteSlots);
            appendSlotRows(declarations, output, pass.name, "DepthSampledRead",
                           pass.depthSampledReadSlots);
            appendSlotRows(declarations, output, pass.name, "TransferRead", pass.transferReadSlots);
            appendSlotRows(declarations, output, pass.name, "TransferWrite",
                           pass.transferWriteSlots);
            appendBufferSlotRows(declarations, output, pass.name, "BufferShaderRead",
                                 pass.bufferReadSlots);
            appendBufferSlotRows(declarations, output, pass.name, "BufferTransferRead",
                                 pass.bufferTransferReadSlots);
            appendBufferSlotRows(declarations, output, pass.name, "BufferTransferWrite",
                                 pass.bufferWriteSlots);
            appendBufferSlotRows(declarations, output, pass.name, "BufferStorageReadWrite",
                                 pass.bufferStorageReadWriteSlots);
        }
    }

    void appendCommandTable(const RenderGraphCompileResult& compiled, std::string& output) {
        output += "\n### RenderGraph Commands\n\n";
        output += "| Pass | # | Command | Detail |\n";
        output += "|---|---:|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            appendCommandRows(output, pass);
        }
    }

    void appendTransitionTable(const RenderGraphDeclarationView& declarations,
                               const RenderGraphCompileResult& compiled, std::string& output) {
        output += "\n### RenderGraph Transitions\n\n";
        output += "| Phase | Pass | Resource | Old State | New State |\n";
        output += "|---|---|---|---|---|\n";
        for (const RenderGraphCompiledPass& pass : compiled.passes) {
            for (const RenderGraphImageTransition& transition : pass.transitionsBefore) {
                appendTransitionRow(declarations, output, "Before", pass.name, transition);
            }
            for (const RenderGraphBufferTransition& transition : pass.bufferTransitionsBefore) {
                appendTransitionRow(declarations, output, "Before", pass.name, transition);
            }
        }
        for (const RenderGraphImageTransition& transition : compiled.finalTransitions) {
            appendTransitionRow(declarations, output, "Final", "-", transition);
        }
        for (const RenderGraphBufferTransition& transition : compiled.finalBufferTransitions) {
            appendTransitionRow(declarations, output, "Final", "-", transition);
        }
    }

    void appendTransientTables(const RenderGraphDeclarationView& declarations,
                               const RenderGraphCompileResult& compiled, std::string& output) {
        output += "\n### RenderGraph Transients\n\n";
        output += "| Image | Format | Extent | First Pass | Last Pass | Final Access |\n";
        output += "|---|---|---:|---:|---:|---|\n";
        for (const RenderGraphTransientImageAllocation& transient : compiled.transientImages) {
            output += "| ";
            output += imageHandleLabel(declarations, transient.image);
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
            output += bufferHandleLabel(declarations, transient.buffer);
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
    }

} // namespace asharia::rendergraph_internal
