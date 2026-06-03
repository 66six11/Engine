#include "render_graph_resource_schema_validation.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/rendergraph/render_graph_types.hpp"

#include "render_graph_pass.hpp"

namespace asharia {

    namespace {

        using DeclaredPass = rendergraph_internal::Pass;

        [[nodiscard]] std::span<const RenderGraphImageSlot>
        slotsForAccess(const DeclaredPass& pass, RenderGraphSlotAccess access) {
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

        [[nodiscard]] std::span<const RenderGraphBufferSlot>
        bufferSlotsForAccess(const DeclaredPass& pass, RenderGraphSlotAccess access) {
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

        [[nodiscard]] const RenderGraphResourceSlotSchema*
        findSlotSchema(const RenderGraphPassSchema& schema, std::string_view name,
                       RenderGraphSlotAccess access, RenderGraphShaderStage shaderStage) {
            for (const RenderGraphResourceSlotSchema& slotSchema : schema.resourceSlots) {
                if (slotSchema.name == name && slotSchema.access == access &&
                    slotSchema.shaderStage == shaderStage) {
                    return &slotSchema;
                }
            }

            return nullptr;
        }

        [[nodiscard]] bool hasSlot(const DeclaredPass& pass,
                                   const RenderGraphResourceSlotSchema& slotSchema) {
            const std::span<const RenderGraphImageSlot> slots =
                slotsForAccess(pass, slotSchema.access);
            const std::span<const RenderGraphBufferSlot> bufferSlots =
                bufferSlotsForAccess(pass, slotSchema.access);

            const auto matchesSchema = [&slotSchema](const auto& slot) {
                return slot.name == slotSchema.name && slot.shaderStage == slotSchema.shaderStage;
            };
            return std::ranges::any_of(slots, matchesSchema) ||
                   std::ranges::any_of(bufferSlots, matchesSchema);
        }

        [[nodiscard]] Result<void> validateSlotsAgainstSchema(
            const DeclaredPass& pass, std::span<const RenderGraphImageSlot> slots,
            RenderGraphSlotAccess access, const RenderGraphPassSchema& schema) {
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

        [[nodiscard]] Result<void> validateSlotsAgainstSchema(
            const DeclaredPass& pass, std::span<const RenderGraphBufferSlot> slots,
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

        [[nodiscard]] Result<void> validateRequiredSlots(const DeclaredPass& pass,
                                                         const RenderGraphPassSchema& schema) {
            for (const RenderGraphResourceSlotSchema& slotSchema : schema.resourceSlots) {
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

            return {};
        }

    } // namespace

    namespace rendergraph_internal {

        // NOLINTBEGIN(readability-function-cognitive-complexity, readability-function-size)
        Result<void> validateResourceSlotsAgainstSchema(const Pass& pass,
                                                        const RenderGraphPassSchema& schema) {
            auto colorSlots = validateSlotsAgainstSchema(pass, pass.colorWriteSlots,
                                                         RenderGraphSlotAccess::ColorWrite, schema);
            if (!colorSlots) {
                return std::unexpected{std::move(colorSlots.error())};
            }

            auto shaderReadSlots = validateSlotsAgainstSchema(
                pass, pass.shaderReadSlots, RenderGraphSlotAccess::ShaderRead, schema);
            if (!shaderReadSlots) {
                return std::unexpected{std::move(shaderReadSlots.error())};
            }

            auto depthReadSlots = validateSlotsAgainstSchema(
                pass, pass.depthReadSlots, RenderGraphSlotAccess::DepthAttachmentRead, schema);
            if (!depthReadSlots) {
                return std::unexpected{std::move(depthReadSlots.error())};
            }

            auto depthWriteSlots = validateSlotsAgainstSchema(
                pass, pass.depthWriteSlots, RenderGraphSlotAccess::DepthAttachmentWrite, schema);
            if (!depthWriteSlots) {
                return std::unexpected{std::move(depthWriteSlots.error())};
            }

            auto depthSampledReadSlots = validateSlotsAgainstSchema(
                pass, pass.depthSampledReadSlots, RenderGraphSlotAccess::DepthSampledRead, schema);
            if (!depthSampledReadSlots) {
                return std::unexpected{std::move(depthSampledReadSlots.error())};
            }

            auto transferReadSlots = validateSlotsAgainstSchema(
                pass, pass.transferReadSlots, RenderGraphSlotAccess::TransferRead, schema);
            if (!transferReadSlots) {
                return std::unexpected{std::move(transferReadSlots.error())};
            }

            auto transferSlots = validateSlotsAgainstSchema(
                pass, pass.transferWriteSlots, RenderGraphSlotAccess::TransferWrite, schema);
            if (!transferSlots) {
                return std::unexpected{std::move(transferSlots.error())};
            }

            auto bufferReadSlots = validateSlotsAgainstSchema(
                pass, pass.bufferReadSlots, RenderGraphSlotAccess::BufferShaderRead, schema);
            if (!bufferReadSlots) {
                return std::unexpected{std::move(bufferReadSlots.error())};
            }

            auto bufferTransferReadSlots =
                validateSlotsAgainstSchema(pass, pass.bufferTransferReadSlots,
                                           RenderGraphSlotAccess::BufferTransferRead, schema);
            if (!bufferTransferReadSlots) {
                return std::unexpected{std::move(bufferTransferReadSlots.error())};
            }

            auto bufferWriteSlots = validateSlotsAgainstSchema(
                pass, pass.bufferWriteSlots, RenderGraphSlotAccess::BufferTransferWrite, schema);
            if (!bufferWriteSlots) {
                return std::unexpected{std::move(bufferWriteSlots.error())};
            }

            auto bufferStorageReadWriteSlots =
                validateSlotsAgainstSchema(pass, pass.bufferStorageReadWriteSlots,
                                           RenderGraphSlotAccess::BufferStorageReadWrite, schema);
            if (!bufferStorageReadWriteSlots) {
                return std::unexpected{std::move(bufferStorageReadWriteSlots.error())};
            }

            return validateRequiredSlots(pass, schema);
        }
        // NOLINTEND(readability-function-cognitive-complexity, readability-function-size)

    } // namespace rendergraph_internal

} // namespace asharia
