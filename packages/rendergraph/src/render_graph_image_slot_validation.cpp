#include "render_graph_image_slot_validation.hpp"

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

        [[nodiscard]] Result<void> validateSlots(std::span<const RenderGraphImageDesc> images,
                                                 const DeclaredPass& pass,
                                                 std::span<const RenderGraphImageSlot> slots) {
            for (std::size_t index = 0; index < slots.size(); ++index) {
                const RenderGraphImageSlot& slot = slots[index];
                if (slot.name.empty()) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' has an unnamed resource slot.",
                    }};
                }

                auto validated = rendergraph_internal::validateImageHandle(images, slot.image);
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
        validateShaderReadSlots(std::span<const RenderGraphImageDesc> images,
                                const DeclaredPass& pass) {
            auto slots = validateSlots(images, pass, pass.shaderReadSlots);
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

        [[nodiscard]] Result<void>
        validateDepthSampledReadSlots(std::span<const RenderGraphImageDesc> images,
                                      const DeclaredPass& pass) {
            auto slots = validateSlots(images, pass, pass.depthSampledReadSlots);
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

    } // namespace

    namespace rendergraph_internal {

        Result<void> validatePassImageSlots(std::span<const RenderGraphImageDesc> images,
                                            const Pass& pass) {
            auto colorSlots = validateSlots(images, pass, pass.colorWriteSlots);
            if (!colorSlots) {
                return std::unexpected{std::move(colorSlots.error())};
            }

            auto shaderReadSlots = validateShaderReadSlots(images, pass);
            if (!shaderReadSlots) {
                return std::unexpected{std::move(shaderReadSlots.error())};
            }

            auto depthReadSlots = validateSlots(images, pass, pass.depthReadSlots);
            if (!depthReadSlots) {
                return std::unexpected{std::move(depthReadSlots.error())};
            }

            auto depthWriteSlots = validateSlots(images, pass, pass.depthWriteSlots);
            if (!depthWriteSlots) {
                return std::unexpected{std::move(depthWriteSlots.error())};
            }

            auto depthSampledReadSlots = validateDepthSampledReadSlots(images, pass);
            if (!depthSampledReadSlots) {
                return std::unexpected{std::move(depthSampledReadSlots.error())};
            }

            auto transferReadSlots = validateSlots(images, pass, pass.transferReadSlots);
            if (!transferReadSlots) {
                return std::unexpected{std::move(transferReadSlots.error())};
            }

            auto transferSlots = validateSlots(images, pass, pass.transferWriteSlots);
            if (!transferSlots) {
                return std::unexpected{std::move(transferSlots.error())};
            }

            return {};
        }

    } // namespace rendergraph_internal

} // namespace asharia
