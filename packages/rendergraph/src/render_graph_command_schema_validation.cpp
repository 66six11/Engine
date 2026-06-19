#include "render_graph_command_schema_validation.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/rendergraph/render_graph_types.hpp"

#include "render_graph_debug_names.hpp"
#include "render_graph_pass.hpp"

namespace asharia {

    namespace {

        [[nodiscard]] bool commandAllowedBySchema(RenderGraphCommandKind commandKind,
                                                  const RenderGraphPassSchema& schema) {
            return std::ranges::any_of(
                schema.allowedCommands,
                [commandKind](RenderGraphCommandKind allowed) { return allowed == commandKind; });
        }

        [[nodiscard]] bool imageSlotExists(std::span<const RenderGraphImageSlot> slots,
                                           std::string_view name) {
            return std::ranges::any_of(
                slots, [name](const RenderGraphImageSlot& slot) { return slot.name == name; });
        }

        [[nodiscard]] bool bufferSlotExists(std::span<const RenderGraphBufferSlot> slots,
                                            std::string_view name) {
            return std::ranges::any_of(
                slots, [name](const RenderGraphBufferSlot& slot) { return slot.name == name; });
        }

        [[nodiscard]] bool textureSlotExists(const rendergraph_internal::Pass& pass,
                                             std::string_view name) {
            return imageSlotExists(pass.shaderReadSlots, name) ||
                   imageSlotExists(pass.depthSampledReadSlots, name);
        }

        [[nodiscard]] bool clearColorSlotExists(const rendergraph_internal::Pass& pass,
                                                std::string_view name) {
            return imageSlotExists(pass.colorWriteSlots, name) ||
                   imageSlotExists(pass.transferWriteSlots, name);
        }

        [[nodiscard]] Result<void> invalidCommandSlot(const rendergraph_internal::Pass& pass,
                                                      const RenderGraphCommand& command,
                                                      std::string_view slotName,
                                                      std::string_view expectedAccess) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph pass '" + pass.name + "' command '" +
                    std::string{rendergraph_internal::commandKindName(command.kind)} +
                    "' references invalid slot '" + std::string{slotName} + "'; expected " +
                    std::string{expectedAccess} + ".",
            }};
        }

        [[nodiscard]] Result<void>
        validateCommandResourceSlots(const rendergraph_internal::Pass& pass,
                                     const RenderGraphCommand& command) {
            switch (command.kind) {
            case RenderGraphCommandKind::SetTexture:
                if (!textureSlotExists(pass, command.secondaryName)) {
                    return invalidCommandSlot(pass, command, command.secondaryName,
                                              "ShaderRead or DepthSampledRead image slot");
                }
                break;
            case RenderGraphCommandKind::ClearColor:
                if (!clearColorSlotExists(pass, command.name)) {
                    return invalidCommandSlot(pass, command, command.name,
                                              "ColorWrite or TransferWrite image slot");
                }
                break;
            case RenderGraphCommandKind::FillBuffer:
                if (!bufferSlotExists(pass.bufferWriteSlots, command.name)) {
                    return invalidCommandSlot(pass, command, command.name,
                                              "BufferTransferWrite slot");
                }
                break;
            case RenderGraphCommandKind::CopyImage:
                if (!imageSlotExists(pass.transferReadSlots, command.name)) {
                    return invalidCommandSlot(pass, command, command.name,
                                              "TransferRead image slot");
                }
                if (!imageSlotExists(pass.transferWriteSlots, command.secondaryName)) {
                    return invalidCommandSlot(pass, command, command.secondaryName,
                                              "TransferWrite image slot");
                }
                break;
            case RenderGraphCommandKind::CopyBuffer:
                if (!bufferSlotExists(pass.bufferTransferReadSlots, command.name)) {
                    return invalidCommandSlot(pass, command, command.name,
                                              "BufferTransferRead slot");
                }
                if (!bufferSlotExists(pass.bufferWriteSlots, command.secondaryName)) {
                    return invalidCommandSlot(pass, command, command.secondaryName,
                                              "BufferTransferWrite slot");
                }
                break;
            case RenderGraphCommandKind::CopyBufferToImage:
                if (!bufferSlotExists(pass.bufferTransferReadSlots, command.name)) {
                    return invalidCommandSlot(pass, command, command.name,
                                              "BufferTransferRead slot");
                }
                if (!imageSlotExists(pass.transferWriteSlots, command.secondaryName)) {
                    return invalidCommandSlot(pass, command, command.secondaryName,
                                              "TransferWrite image slot");
                }
                break;
            case RenderGraphCommandKind::CopyImageToBuffer:
                if (!imageSlotExists(pass.transferReadSlots, command.name)) {
                    return invalidCommandSlot(pass, command, command.name,
                                              "TransferRead image slot");
                }
                if (!bufferSlotExists(pass.bufferWriteSlots, command.secondaryName)) {
                    return invalidCommandSlot(pass, command, command.secondaryName,
                                              "BufferTransferWrite slot");
                }
                break;
            case RenderGraphCommandKind::SetShader:
            case RenderGraphCommandKind::SetFloat:
            case RenderGraphCommandKind::SetInt:
            case RenderGraphCommandKind::SetVec4:
            case RenderGraphCommandKind::DrawFullscreenTriangle:
            case RenderGraphCommandKind::Dispatch:
                break;
            }

            return {};
        }

    } // namespace

    namespace rendergraph_internal {

        Result<void> validateCommandsAgainstSchema(const Pass& pass,
                                                   const RenderGraphPassSchema& schema) {
            for (const RenderGraphCommand& command : pass.commands) {
                if (!commandAllowedBySchema(command.kind, schema)) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' command '" +
                            std::string{commandKindName(command.kind)} +
                            "' is not allowed by schema '" + schema.type + "'.",
                    }};
                }

                auto resources = validateCommandResourceSlots(pass, command);
                if (!resources) {
                    return std::unexpected{std::move(resources.error())};
                }
            }

            return {};
        }

    } // namespace rendergraph_internal

} // namespace asharia
