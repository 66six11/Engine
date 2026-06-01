#include "render_graph_validation.hpp"

#include <span>
#include <utility>

#include "render_graph_schema_validation.hpp"
#include "render_graph_slot_validation.hpp"

namespace asharia::rendergraph_internal {

    Result<void> validateImageHandle(std::span<const RenderGraphImageDesc> images,
                                     RenderGraphImageHandle image) {
        if (image.index >= images.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph image handle is out of range.",
            }};
        }

        return {};
    }

    Result<void> validateBufferHandle(std::span<const RenderGraphBufferDesc> buffers,
                                      RenderGraphBufferHandle buffer) {
        if (buffer.index >= buffers.size()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph buffer handle is out of range.",
            }};
        }

        return {};
    }

    Result<void> validateImages(std::span<const RenderGraphImageDesc> images) {
        for (const RenderGraphImageDesc& image : images) {
            if (image.lifetime == RenderGraphImageLifetime::Imported &&
                image.finalState == RenderGraphImageState::Undefined) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Imported render graph image '" + image.name +
                        "' must declare an explicit final state.",
                }};
            }
        }

        return {};
    }

    Result<void> validateBuffers(std::span<const RenderGraphBufferDesc> buffers) {
        for (const RenderGraphBufferDesc& buffer : buffers) {
            if (buffer.byteSize == 0) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph buffer '" + buffer.name + "' must declare a non-zero byte size.",
                }};
            }
            if (buffer.lifetime == RenderGraphBufferLifetime::Imported &&
                buffer.finalState == RenderGraphBufferState::Undefined) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Imported render graph buffer '" + buffer.name +
                        "' must declare an explicit final state.",
                }};
            }
        }

        return {};
    }

    Result<void> validatePass(std::span<const RenderGraphImageDesc> images,
                              std::span<const RenderGraphBufferDesc> buffers, const Pass& pass,
                              const RenderGraphSchemaRegistry* schemaRegistry) {
        auto slotsValidated = validatePassSlots(images, buffers, pass);
        if (!slotsValidated) {
            return std::unexpected{std::move(slotsValidated.error())};
        }

        if (schemaRegistry != nullptr) {
            auto schemaValidated = validatePassSchema(pass, *schemaRegistry);
            if (!schemaValidated) {
                return std::unexpected{std::move(schemaValidated.error())};
            }
        }

        return {};
    }

} // namespace asharia::rendergraph_internal
