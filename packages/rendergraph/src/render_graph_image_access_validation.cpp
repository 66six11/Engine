#include "render_graph_image_access_validation.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "render_graph_pass.hpp"

namespace asharia {

    namespace {

        using DeclaredPass = rendergraph_internal::Pass;

        struct ImageSlotGroup {
            std::string_view access;
            std::span<const RenderGraphImageSlot> slots;
        };

        [[nodiscard]] std::string
        validationImageHandleLabel(std::span<const RenderGraphImageDesc> images,
                                   RenderGraphImageHandle image) {
            std::string label = "#";
            label += std::to_string(image.index);
            if (image.index < images.size() && !images[image.index].name.empty()) {
                label += " ";
                label += images[image.index].name;
            }
            return label;
        }

        [[nodiscard]] std::string
        imageAccessConflictMessage(std::span<const RenderGraphImageDesc> images,
                                   const DeclaredPass& pass, const RenderGraphImageSlot& slot,
                                   std::string_view access, const RenderGraphImageSlot& otherSlot,
                                   std::string_view otherAccess) {
            std::string message = "Render graph pass '";
            message += pass.name;
            message += "' declares image '";
            message += validationImageHandleLabel(images, slot.image);
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

        [[nodiscard]] Result<void> validateUniqueImageAccesses(
            std::span<const RenderGraphImageDesc> images, const DeclaredPass& pass,
            std::span<const std::span<const RenderGraphImageSlot>> slotGroups) {
            const std::array<ImageSlotGroup, 7> namedGroups{
                ImageSlotGroup{
                    .access = "ColorWrite",
                    .slots = slotGroups[0],
                },
                ImageSlotGroup{
                    .access = "ShaderRead",
                    .slots = slotGroups[1],
                },
                ImageSlotGroup{
                    .access = "DepthAttachmentRead",
                    .slots = slotGroups[2],
                },
                ImageSlotGroup{
                    .access = "DepthAttachmentWrite",
                    .slots = slotGroups[3],
                },
                ImageSlotGroup{
                    .access = "DepthSampledRead",
                    .slots = slotGroups[4],
                },
                ImageSlotGroup{
                    .access = "TransferRead",
                    .slots = slotGroups[5],
                },
                ImageSlotGroup{
                    .access = "TransferWrite",
                    .slots = slotGroups[6],
                },
            };

            for (auto groupIt = namedGroups.begin(); groupIt != namedGroups.end(); ++groupIt) {
                const ImageSlotGroup& group = *groupIt;
                for (std::size_t slotIndex = 0; slotIndex < group.slots.size(); ++slotIndex) {
                    const RenderGraphImageSlot& slot = group.slots[slotIndex];
                    for (auto otherGroupIt = groupIt; otherGroupIt != namedGroups.end();
                         ++otherGroupIt) {
                        const ImageSlotGroup& otherGroup = *otherGroupIt;
                        const std::size_t otherSlotBegin =
                            otherGroupIt == groupIt ? slotIndex + 1 : 0;
                        for (std::size_t otherSlotIndex = otherSlotBegin;
                             otherSlotIndex < otherGroup.slots.size(); ++otherSlotIndex) {
                            const RenderGraphImageSlot& otherSlot =
                                otherGroup.slots[otherSlotIndex];
                            if (slot.image == otherSlot.image) {
                                return std::unexpected{Error{
                                    ErrorDomain::RenderGraph,
                                    0,
                                    imageAccessConflictMessage(images, pass, slot, group.access,
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

        Result<void> validateUniquePassImageAccesses(std::span<const RenderGraphImageDesc> images,
                                                     const Pass& pass) {
            const std::array<std::span<const RenderGraphImageSlot>, 7> slotGroups{
                pass.colorWriteSlots,    pass.shaderReadSlots,       pass.depthReadSlots,
                pass.depthWriteSlots,    pass.depthSampledReadSlots, pass.transferReadSlots,
                pass.transferWriteSlots,
            };
            return validateUniqueImageAccesses(images, pass, slotGroups);
        }

    } // namespace rendergraph_internal

} // namespace asharia
