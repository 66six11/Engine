#include "render_graph_buffer_access_validation.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "render_graph_pass.hpp"

namespace asharia {

    namespace {

        using DeclaredPass = rendergraph_internal::Pass;

        struct BufferSlotGroup {
            std::string_view access;
            std::span<const RenderGraphBufferSlot> slots;
        };

        [[nodiscard]] std::string
        validationBufferHandleLabel(std::span<const RenderGraphBufferDesc> buffers,
                                    RenderGraphBufferHandle buffer) {
            std::string label = "#";
            label += std::to_string(buffer.index);
            if (buffer.index < buffers.size() && !buffers[buffer.index].name.empty()) {
                label += " ";
                label += buffers[buffer.index].name;
            }
            return label;
        }

        [[nodiscard]] std::string
        bufferAccessConflictMessage(std::span<const RenderGraphBufferDesc> buffers,
                                    const DeclaredPass& pass, const RenderGraphBufferSlot& slot,
                                    std::string_view access, const RenderGraphBufferSlot& otherSlot,
                                    std::string_view otherAccess) {
            std::string message = "Render graph pass '";
            message += pass.name;
            message += "' declares buffer '";
            message += validationBufferHandleLabel(buffers, slot.buffer);
            message += "' more than once in slots '";
            message += slot.name;
            message += "' (";
            message += access;
            message += ") and '";
            message += otherSlot.name;
            message += "' (";
            message += otherAccess;
            message += "). Split the operation into separate passes or add an explicit combined "
                       "access state.";
            return message;
        }

        [[nodiscard]] Result<void>
        validateUniqueBufferAccesses(std::span<const RenderGraphBufferDesc> buffers,
                                     const DeclaredPass& pass) {
            const std::array<BufferSlotGroup, 4> namedGroups{
                BufferSlotGroup{
                    .access = "BufferShaderRead",
                    .slots = pass.bufferReadSlots,
                },
                BufferSlotGroup{
                    .access = "BufferTransferRead",
                    .slots = pass.bufferTransferReadSlots,
                },
                BufferSlotGroup{
                    .access = "BufferTransferWrite",
                    .slots = pass.bufferWriteSlots,
                },
                BufferSlotGroup{
                    .access = "BufferStorageReadWrite",
                    .slots = pass.bufferStorageReadWriteSlots,
                },
            };

            for (auto groupIt = namedGroups.begin(); groupIt != namedGroups.end(); ++groupIt) {
                const BufferSlotGroup& group = *groupIt;
                for (std::size_t slotIndex = 0; slotIndex < group.slots.size(); ++slotIndex) {
                    const RenderGraphBufferSlot& slot = group.slots[slotIndex];
                    for (auto otherGroupIt = groupIt; otherGroupIt != namedGroups.end();
                         ++otherGroupIt) {
                        const BufferSlotGroup& otherGroup = *otherGroupIt;
                        const std::size_t otherSlotBegin =
                            otherGroupIt == groupIt ? slotIndex + 1 : 0;
                        for (std::size_t otherSlotIndex = otherSlotBegin;
                             otherSlotIndex < otherGroup.slots.size(); ++otherSlotIndex) {
                            const RenderGraphBufferSlot& otherSlot =
                                otherGroup.slots[otherSlotIndex];
                            if (slot.buffer == otherSlot.buffer) {
                                return std::unexpected{Error{
                                    ErrorDomain::RenderGraph,
                                    0,
                                    bufferAccessConflictMessage(buffers, pass, slot, group.access,
                                                                otherSlot, otherGroup.access),
                                }};
                            }
                        }
                    }
                }
            }

            return {};
        }

    } // namespace

    namespace rendergraph_internal {

        Result<void>
        validateUniquePassBufferAccesses(std::span<const RenderGraphBufferDesc> buffers,
                                         const Pass& pass) {
            return validateUniqueBufferAccesses(buffers, pass);
        }

    } // namespace rendergraph_internal

} // namespace asharia
