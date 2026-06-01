#include "render_graph_buffer_slot_validation.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "render_graph_pass.hpp"
#include "render_graph_validation.hpp"

namespace asharia {

    namespace {

        using DeclaredPass = rendergraph_internal::Pass;

        [[nodiscard]] std::string duplicateSlotMessage(const DeclaredPass& pass,
                                                       std::string_view slotName) {
            std::string message = "Render graph pass '";
            message += pass.name;
            message += "' declares duplicate resource slot '";
            message += slotName;
            message += "'.";
            return message;
        }

        [[nodiscard]] Result<void> validateSlots(std::span<const RenderGraphBufferDesc> buffers,
                                                 const DeclaredPass& pass,
                                                 std::span<const RenderGraphBufferSlot> slots) {
            for (std::size_t index = 0; index < slots.size(); ++index) {
                const RenderGraphBufferSlot& slot = slots[index];
                if (slot.name.empty()) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' has an unnamed resource slot.",
                    }};
                }

                auto validated = rendergraph_internal::validateBufferHandle(buffers, slot.buffer);
                if (!validated) {
                    return std::unexpected{std::move(validated.error())};
                }

                for (std::size_t otherIndex = index + 1; otherIndex < slots.size(); ++otherIndex) {
                    if (slot.name == slots[otherIndex].name) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            duplicateSlotMessage(pass, slot.name),
                        }};
                    }
                }
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateBufferReadSlots(std::span<const RenderGraphBufferDesc> buffers,
                                const DeclaredPass& pass) {
            auto slots = validateSlots(buffers, pass, pass.bufferReadSlots);
            if (!slots) {
                return std::unexpected{std::move(slots.error())};
            }

            for (const RenderGraphBufferSlot& slot : pass.bufferReadSlots) {
                if (slot.shaderStage == RenderGraphShaderStage::None) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' declares buffer shader read slot '" +
                            slot.name + "' without a shader stage.",
                    }};
                }
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateBufferStorageReadWriteSlots(std::span<const RenderGraphBufferDesc> buffers,
                                            const DeclaredPass& pass) {
            auto slots = validateSlots(buffers, pass, pass.bufferStorageReadWriteSlots);
            if (!slots) {
                return std::unexpected{std::move(slots.error())};
            }

            for (const RenderGraphBufferSlot& slot : pass.bufferStorageReadWriteSlots) {
                if (slot.shaderStage == RenderGraphShaderStage::None) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name +
                            "' declares buffer storage read/write slot '" + slot.name +
                            "' without a shader stage.",
                    }};
                }
            }

            return {};
        }

    } // namespace

    namespace rendergraph_internal {

        Result<void> validatePassBufferSlots(std::span<const RenderGraphBufferDesc> buffers,
                                             const Pass& pass) {
            auto bufferReadSlots = validateBufferReadSlots(buffers, pass);
            if (!bufferReadSlots) {
                return std::unexpected{std::move(bufferReadSlots.error())};
            }

            auto bufferTransferReadSlots =
                validateSlots(buffers, pass, pass.bufferTransferReadSlots);
            if (!bufferTransferReadSlots) {
                return std::unexpected{std::move(bufferTransferReadSlots.error())};
            }

            auto bufferWriteSlots = validateSlots(buffers, pass, pass.bufferWriteSlots);
            if (!bufferWriteSlots) {
                return std::unexpected{std::move(bufferWriteSlots.error())};
            }

            auto bufferStorageReadWriteSlots = validateBufferStorageReadWriteSlots(buffers, pass);
            if (!bufferStorageReadWriteSlots) {
                return std::unexpected{std::move(bufferStorageReadWriteSlots.error())};
            }

            return {};
        }

    } // namespace rendergraph_internal

} // namespace asharia
