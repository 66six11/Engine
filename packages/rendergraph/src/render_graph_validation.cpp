#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "render_graph_internal.hpp"

namespace asharia {

    namespace {

        bool commandAllowedBySchema(RenderGraphCommandKind commandKind,
                                    const RenderGraphPassSchema& schema) {
            return std::ranges::any_of(
                schema.allowedCommands,
                [commandKind](RenderGraphCommandKind allowed) { return allowed == commandKind; });
        }

    } // namespace

    Result<void> RenderGraph::Impl::validateImageHandle(RenderGraphImageHandle image) const {
        if (image.index >= images_.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph image handle is out of range.",
            }};
        }

        return {};
    }

    Result<void> RenderGraph::Impl::validateBufferHandle(RenderGraphBufferHandle buffer) const {
        if (buffer.index >= buffers_.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph buffer handle is out of range.",
            }};
        }

        return {};
    }

    // NOLINTBEGIN(readability-function-cognitive-complexity, readability-function-size)
    Result<void> RenderGraph::Impl::validateWriteSlots(const Pass& pass) const {
        auto colorSlots = validateSlots(pass, pass.colorWriteSlots);
        if (!colorSlots) {
            return std::unexpected{std::move(colorSlots.error())};
        }

        auto shaderReadSlots = validateShaderReadSlots(pass);
        if (!shaderReadSlots) {
            return std::unexpected{std::move(shaderReadSlots.error())};
        }

        auto depthReadSlots = validateSlots(pass, pass.depthReadSlots);
        if (!depthReadSlots) {
            return std::unexpected{std::move(depthReadSlots.error())};
        }

        auto depthWriteSlots = validateSlots(pass, pass.depthWriteSlots);
        if (!depthWriteSlots) {
            return std::unexpected{std::move(depthWriteSlots.error())};
        }

        auto depthSampledReadSlots = validateDepthSampledReadSlots(pass);
        if (!depthSampledReadSlots) {
            return std::unexpected{std::move(depthSampledReadSlots.error())};
        }

        auto transferReadSlots = validateSlots(pass, pass.transferReadSlots);
        if (!transferReadSlots) {
            return std::unexpected{std::move(transferReadSlots.error())};
        }

        auto transferSlots = validateSlots(pass, pass.transferWriteSlots);
        if (!transferSlots) {
            return std::unexpected{std::move(transferSlots.error())};
        }

        auto bufferReadSlots = validateBufferReadSlots(pass);
        if (!bufferReadSlots) {
            return std::unexpected{std::move(bufferReadSlots.error())};
        }

        auto bufferTransferReadSlots = validateSlots(pass, pass.bufferTransferReadSlots);
        if (!bufferTransferReadSlots) {
            return std::unexpected{std::move(bufferTransferReadSlots.error())};
        }

        auto bufferWriteSlots = validateSlots(pass, pass.bufferWriteSlots);
        if (!bufferWriteSlots) {
            return std::unexpected{std::move(bufferWriteSlots.error())};
        }

        auto bufferStorageReadWriteSlots = validateBufferStorageReadWriteSlots(pass);
        if (!bufferStorageReadWriteSlots) {
            return std::unexpected{std::move(bufferStorageReadWriteSlots.error())};
        }

        const std::array<std::span<const RenderGraphImageSlot>, 7> slotGroups{
            pass.colorWriteSlots,    pass.shaderReadSlots,       pass.depthReadSlots,
            pass.depthWriteSlots,    pass.depthSampledReadSlots, pass.transferReadSlots,
            pass.transferWriteSlots,
        };
        auto duplicateSlots = validateUniqueResourceSlotNames(pass);
        if (!duplicateSlots) {
            return std::unexpected{std::move(duplicateSlots.error())};
        }

        auto imageAccesses = validateUniqueImageAccesses(pass, slotGroups);
        if (!imageAccesses) {
            return std::unexpected{std::move(imageAccesses.error())};
        }

        auto bufferAccesses = validateUniqueBufferAccesses(pass);
        if (!bufferAccesses) {
            return std::unexpected{std::move(bufferAccesses.error())};
        }

        return {};
    }

    Result<void> RenderGraph::Impl::validateUniqueImageAccesses(
        const Pass& pass, std::span<const std::span<const RenderGraphImageSlot>> slotGroups) const {
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
                    const std::size_t otherSlotBegin = otherGroupIt == groupIt ? slotIndex + 1 : 0;
                    for (std::size_t otherSlotIndex = otherSlotBegin;
                         otherSlotIndex < otherGroup.slots.size(); ++otherSlotIndex) {
                        const RenderGraphImageSlot& otherSlot = otherGroup.slots[otherSlotIndex];
                        if (slot.image == otherSlot.image) {
                            return std::unexpected{Error{
                                ErrorDomain::RenderGraph,
                                0,
                                imageAccessConflictMessage(pass, slot, group.access, otherSlot,
                                                           otherGroup.access),
                            }};
                        }
                    }
                }
            }
        }

        return {};
    }

    Result<void> RenderGraph::Impl::validateUniqueBufferAccesses(const Pass& pass) const {
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
                    const std::size_t otherSlotBegin = otherGroupIt == groupIt ? slotIndex + 1 : 0;
                    for (std::size_t otherSlotIndex = otherSlotBegin;
                         otherSlotIndex < otherGroup.slots.size(); ++otherSlotIndex) {
                        const RenderGraphBufferSlot& otherSlot = otherGroup.slots[otherSlotIndex];
                        if (slot.buffer == otherSlot.buffer) {
                            return std::unexpected{Error{
                                ErrorDomain::RenderGraph,
                                0,
                                bufferAccessConflictMessage(pass, slot, group.access, otherSlot,
                                                            otherGroup.access),
                            }};
                        }
                    }
                }
            }
        }

        return {};
    }

    Result<void>
    RenderGraph::Impl::validateSchema(const Pass& pass,
                                      const RenderGraphSchemaRegistry& schemaRegistry) {
        if (pass.type.empty()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph pass '" + pass.name + "' cannot be schema-validated without a type.",
            }};
        }

        const RenderGraphPassSchema* schema = schemaRegistry.find(pass.type);
        if (schema == nullptr) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph pass '" + pass.name + "' has no registered schema for type '" +
                    pass.type + "'.",
            }};
        }

        if (pass.paramsType != schema->paramsType) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph pass '" + pass.name + "' expected params type '" +
                    schema->paramsType + "' but found '" + pass.paramsType + "'.",
            }};
        }

        auto colorSlots = validateSlotsAgainstSchema(pass, pass.colorWriteSlots,
                                                     RenderGraphSlotAccess::ColorWrite, *schema);
        if (!colorSlots) {
            return std::unexpected{std::move(colorSlots.error())};
        }

        auto shaderReadSlots = validateSlotsAgainstSchema(
            pass, pass.shaderReadSlots, RenderGraphSlotAccess::ShaderRead, *schema);
        if (!shaderReadSlots) {
            return std::unexpected{std::move(shaderReadSlots.error())};
        }

        auto depthReadSlots = validateSlotsAgainstSchema(
            pass, pass.depthReadSlots, RenderGraphSlotAccess::DepthAttachmentRead, *schema);
        if (!depthReadSlots) {
            return std::unexpected{std::move(depthReadSlots.error())};
        }

        auto depthWriteSlots = validateSlotsAgainstSchema(
            pass, pass.depthWriteSlots, RenderGraphSlotAccess::DepthAttachmentWrite, *schema);
        if (!depthWriteSlots) {
            return std::unexpected{std::move(depthWriteSlots.error())};
        }

        auto depthSampledReadSlots = validateSlotsAgainstSchema(
            pass, pass.depthSampledReadSlots, RenderGraphSlotAccess::DepthSampledRead, *schema);
        if (!depthSampledReadSlots) {
            return std::unexpected{std::move(depthSampledReadSlots.error())};
        }

        auto transferReadSlots = validateSlotsAgainstSchema(
            pass, pass.transferReadSlots, RenderGraphSlotAccess::TransferRead, *schema);
        if (!transferReadSlots) {
            return std::unexpected{std::move(transferReadSlots.error())};
        }

        auto transferSlots = validateSlotsAgainstSchema(
            pass, pass.transferWriteSlots, RenderGraphSlotAccess::TransferWrite, *schema);
        if (!transferSlots) {
            return std::unexpected{std::move(transferSlots.error())};
        }

        auto bufferReadSlots = validateSlotsAgainstSchema(
            pass, pass.bufferReadSlots, RenderGraphSlotAccess::BufferShaderRead, *schema);
        if (!bufferReadSlots) {
            return std::unexpected{std::move(bufferReadSlots.error())};
        }

        auto bufferTransferReadSlots = validateSlotsAgainstSchema(
            pass, pass.bufferTransferReadSlots, RenderGraphSlotAccess::BufferTransferRead, *schema);
        if (!bufferTransferReadSlots) {
            return std::unexpected{std::move(bufferTransferReadSlots.error())};
        }

        auto bufferWriteSlots = validateSlotsAgainstSchema(
            pass, pass.bufferWriteSlots, RenderGraphSlotAccess::BufferTransferWrite, *schema);
        if (!bufferWriteSlots) {
            return std::unexpected{std::move(bufferWriteSlots.error())};
        }

        auto bufferStorageReadWriteSlots =
            validateSlotsAgainstSchema(pass, pass.bufferStorageReadWriteSlots,
                                       RenderGraphSlotAccess::BufferStorageReadWrite, *schema);
        if (!bufferStorageReadWriteSlots) {
            return std::unexpected{std::move(bufferStorageReadWriteSlots.error())};
        }

        for (const RenderGraphResourceSlotSchema& slotSchema : schema->resourceSlots) {
            if (slotSchema.optional) {
                continue;
            }
            if (!hasSlot(pass, slotSchema)) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' is missing required slot '" +
                        slotSchema.name + "'.",
                }};
            }
        }

        auto commands = validateCommandsAgainstSchema(pass, *schema);
        if (!commands) {
            return std::unexpected{std::move(commands.error())};
        }

        return {};
    }
    // NOLINTEND(readability-function-cognitive-complexity, readability-function-size)

    Result<void>
    RenderGraph::Impl::validateCommandsAgainstSchema(const Pass& pass,
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
        }

        return {};
    }

    Result<void> RenderGraph::Impl::validateSlotsAgainstSchema(
        const Pass& pass, std::span<const RenderGraphImageSlot> slots, RenderGraphSlotAccess access,
        const RenderGraphPassSchema& schema) {
        for (const RenderGraphImageSlot& slot : slots) {
            if (findSlotSchema(schema, slot.name, access, slot.shaderStage) == nullptr) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' declares slot '" + slot.name +
                        "' that is not allowed by schema '" + schema.type + "'.",
                }};
            }
        }

        return {};
    }

    Result<void> RenderGraph::Impl::validateSlotsAgainstSchema(
        const Pass& pass, std::span<const RenderGraphBufferSlot> slots,
        RenderGraphSlotAccess access, const RenderGraphPassSchema& schema) {
        for (const RenderGraphBufferSlot& slot : slots) {
            if (findSlotSchema(schema, slot.name, access, slot.shaderStage) == nullptr) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' declares slot '" + slot.name +
                        "' that is not allowed by schema '" + schema.type + "'.",
                }};
            }
        }

        return {};
    }

    bool RenderGraph::Impl::hasSlot(const Pass& pass,
                                    const RenderGraphResourceSlotSchema& slotSchema) {
        const std::span<const RenderGraphImageSlot> slots = slotsForAccess(pass, slotSchema.access);
        const std::span<const RenderGraphBufferSlot> bufferSlots =
            bufferSlotsForAccess(pass, slotSchema.access);

        const auto matchesSchema = [&slotSchema](const auto& slot) {
            return slot.name == slotSchema.name && slot.shaderStage == slotSchema.shaderStage;
        };
        return std::ranges::any_of(slots, matchesSchema) ||
               std::ranges::any_of(bufferSlots, matchesSchema);
    }

    const RenderGraphResourceSlotSchema*
    RenderGraph::Impl::findSlotSchema(const RenderGraphPassSchema& schema, std::string_view name,
                                      RenderGraphSlotAccess access,
                                      RenderGraphShaderStage shaderStage) {
        for (const RenderGraphResourceSlotSchema& slotSchema : schema.resourceSlots) {
            if (slotSchema.name == name && slotSchema.access == access &&
                slotSchema.shaderStage == shaderStage) {
                return &slotSchema;
            }
        }

        return nullptr;
    }

    std::span<const RenderGraphImageSlot>
    RenderGraph::Impl::slotsForAccess(const Pass& pass, RenderGraphSlotAccess access) {
        switch (access) {
        case RenderGraphSlotAccess::ColorWrite:
            return pass.colorWriteSlots;
        case RenderGraphSlotAccess::ShaderRead:
            return pass.shaderReadSlots;
        case RenderGraphSlotAccess::DepthAttachmentRead:
            return pass.depthReadSlots;
        case RenderGraphSlotAccess::DepthAttachmentWrite:
            return pass.depthWriteSlots;
        case RenderGraphSlotAccess::DepthSampledRead:
            return pass.depthSampledReadSlots;
        case RenderGraphSlotAccess::TransferRead:
            return pass.transferReadSlots;
        case RenderGraphSlotAccess::TransferWrite:
            return pass.transferWriteSlots;
        case RenderGraphSlotAccess::BufferShaderRead:
        case RenderGraphSlotAccess::BufferTransferRead:
        case RenderGraphSlotAccess::BufferTransferWrite:
        case RenderGraphSlotAccess::BufferStorageReadWrite:
            return {};
        }
        return {};
    }

    std::span<const RenderGraphBufferSlot>
    RenderGraph::Impl::bufferSlotsForAccess(const Pass& pass, RenderGraphSlotAccess access) {
        switch (access) {
        case RenderGraphSlotAccess::BufferShaderRead:
            return pass.bufferReadSlots;
        case RenderGraphSlotAccess::BufferTransferRead:
            return pass.bufferTransferReadSlots;
        case RenderGraphSlotAccess::BufferTransferWrite:
            return pass.bufferWriteSlots;
        case RenderGraphSlotAccess::BufferStorageReadWrite:
            return pass.bufferStorageReadWriteSlots;
        case RenderGraphSlotAccess::ColorWrite:
        case RenderGraphSlotAccess::ShaderRead:
        case RenderGraphSlotAccess::DepthAttachmentRead:
        case RenderGraphSlotAccess::DepthAttachmentWrite:
        case RenderGraphSlotAccess::DepthSampledRead:
        case RenderGraphSlotAccess::TransferRead:
        case RenderGraphSlotAccess::TransferWrite:
            return {};
        }
        return {};
    }

    Result<void>
    RenderGraph::Impl::validateSlots(const Pass& pass,
                                     std::span<const RenderGraphImageSlot> slots) const {
        for (std::size_t index = 0; index < slots.size(); ++index) {
            const RenderGraphImageSlot& slot = slots[index];
            if (slot.name.empty()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' has an unnamed resource slot.",
                }};
            }

            auto validated = validateImageHandle(slot.image);
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

    Result<void>
    RenderGraph::Impl::validateSlots(const Pass& pass,
                                     std::span<const RenderGraphBufferSlot> slots) const {
        for (std::size_t index = 0; index < slots.size(); ++index) {
            const RenderGraphBufferSlot& slot = slots[index];
            if (slot.name.empty()) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' has an unnamed resource slot.",
                }};
            }

            auto validated = validateBufferHandle(slot.buffer);
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

    Result<void> RenderGraph::Impl::validateShaderReadSlots(const Pass& pass) const {
        auto slots = validateSlots(pass, pass.shaderReadSlots);
        if (!slots) {
            return std::unexpected{std::move(slots.error())};
        }

        for (const RenderGraphImageSlot& slot : pass.shaderReadSlots) {
            if (slot.shaderStage == RenderGraphShaderStage::None) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' declares shader read slot '" +
                        slot.name + "' without a shader stage.",
                }};
            }
        }

        return {};
    }

    Result<void> RenderGraph::Impl::validateDepthSampledReadSlots(const Pass& pass) const {
        auto slots = validateSlots(pass, pass.depthSampledReadSlots);
        if (!slots) {
            return std::unexpected{std::move(slots.error())};
        }

        for (const RenderGraphImageSlot& slot : pass.depthSampledReadSlots) {
            if (slot.shaderStage == RenderGraphShaderStage::None) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph pass '" + pass.name + "' declares depth sampled read slot '" +
                        slot.name + "' without a shader stage.",
                }};
            }
        }

        return {};
    }

    Result<void> RenderGraph::Impl::validateBufferReadSlots(const Pass& pass) const {
        auto slots = validateSlots(pass, pass.bufferReadSlots);
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

    Result<void> RenderGraph::Impl::validateBufferStorageReadWriteSlots(const Pass& pass) const {
        auto slots = validateSlots(pass, pass.bufferStorageReadWriteSlots);
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

    // NOLINTBEGIN(readability-function-cognitive-complexity)
    Result<void> RenderGraph::Impl::validateUniqueResourceSlotNames(const Pass& pass) {
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

} // namespace asharia
