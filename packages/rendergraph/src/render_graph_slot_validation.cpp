#include "render_graph_slot_validation.hpp"

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "render_graph_buffer_access_validation.hpp"
#include "render_graph_buffer_slot_validation.hpp"
#include "render_graph_image_access_validation.hpp"
#include "render_graph_image_slot_validation.hpp"
#include "render_graph_pass.hpp"

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

        // NOLINTBEGIN(readability-function-cognitive-complexity)
        [[nodiscard]] Result<void> validateUniqueResourceSlotNames(const DeclaredPass& pass) {
            std::vector<std::string_view> names;
            const auto addName = [&](std::string_view name) -> Result<void> {
                for (const std::string_view existing : names) {
                    if (existing == name) {
                        return std::unexpected{Error{
                            ErrorDomain::RenderGraph,
                            0,
                            duplicateSlotMessage(pass, name),
                        }};
                    }
                }
                names.push_back(name);
                return {};
            };

            for (const RenderGraphImageSlot& slot : pass.colorWriteSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphImageSlot& slot : pass.shaderReadSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphImageSlot& slot : pass.depthReadSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphImageSlot& slot : pass.depthWriteSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphImageSlot& slot : pass.depthSampledReadSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphImageSlot& slot : pass.transferReadSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphImageSlot& slot : pass.transferWriteSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphBufferSlot& slot : pass.bufferReadSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphBufferSlot& slot : pass.bufferTransferReadSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphBufferSlot& slot : pass.bufferWriteSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }
            for (const RenderGraphBufferSlot& slot : pass.bufferStorageReadWriteSlots) {
                auto added = addName(slot.name);
                if (!added) {
                    return added;
                }
            }

            return {};
        }
        // NOLINTEND(readability-function-cognitive-complexity)

    } // namespace

    namespace rendergraph_internal {

        Result<void> validatePassSlots(std::span<const RenderGraphImageDesc> images,
                                       std::span<const RenderGraphBufferDesc> buffers,
                                       const Pass& pass) {
            auto imageSlots = validatePassImageSlots(images, pass);
            if (!imageSlots) {
                return std::unexpected{std::move(imageSlots.error())};
            }

            auto bufferSlots = validatePassBufferSlots(buffers, pass);
            if (!bufferSlots) {
                return std::unexpected{std::move(bufferSlots.error())};
            }

            auto duplicateSlots = validateUniqueResourceSlotNames(pass);
            if (!duplicateSlots) {
                return std::unexpected{std::move(duplicateSlots.error())};
            }

            auto imageAccesses = validateUniquePassImageAccesses(images, pass);
            if (!imageAccesses) {
                return std::unexpected{std::move(imageAccesses.error())};
            }

            auto bufferAccesses = validateUniquePassBufferAccesses(buffers, pass);
            if (!bufferAccesses) {
                return std::unexpected{std::move(bufferAccesses.error())};
            }

            return {};
        }

    } // namespace rendergraph_internal

} // namespace asharia
