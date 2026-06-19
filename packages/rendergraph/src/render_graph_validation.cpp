#include "render_graph_validation.hpp"

#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "render_graph_schema_validation.hpp"
#include "render_graph_slot_validation.hpp"

namespace asharia::rendergraph_internal {
    namespace {

        [[nodiscard]] bool requiresShaderStage(RenderGraphImageState state) {
            return state == RenderGraphImageState::ShaderRead ||
                   state == RenderGraphImageState::DepthSampledRead;
        }

        [[nodiscard]] bool requiresShaderStage(RenderGraphBufferState state) {
            return state == RenderGraphBufferState::ShaderRead ||
                   state == RenderGraphBufferState::StorageReadWrite;
        }

        [[nodiscard]] Result<void>
        validateImageShaderStage(std::string_view imageName, RenderGraphImageState state,
                                 RenderGraphShaderStage shaderStage) {
            if (requiresShaderStage(state) && shaderStage == RenderGraphShaderStage::None) {
                const std::string stateName =
                    state == RenderGraphImageState::DepthSampledRead ? "DepthSampledRead"
                                                                     : "ShaderRead";
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph image '" + std::string{imageName} + "' " + stateName +
                        " state must declare a shader stage.",
                }};
            }

            return {};
        }

        [[nodiscard]] Result<void>
        validateBufferShaderStage(std::string_view bufferName, RenderGraphBufferState state,
                                  RenderGraphShaderStage shaderStage) {
            if (requiresShaderStage(state) && shaderStage == RenderGraphShaderStage::None) {
                const std::string stateName =
                    state == RenderGraphBufferState::StorageReadWrite ? "StorageReadWrite"
                                                                      : "ShaderRead";
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph buffer '" + std::string{bufferName} + "' " + stateName +
                        " state must declare a shader stage.",
                }};
            }

            return {};
        }

    } // namespace


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
            if (image.format == RenderGraphImageFormat::Undefined) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph image '" + image.name + "' must declare a defined format.",
                }};
            }
            if (image.extent.width == 0 || image.extent.height == 0) {
                return std::unexpected{Error{
                    ErrorDomain::RenderGraph,
                    0,
                    "Render graph image '" + image.name + "' must declare a non-zero extent.",
                }};
            }
            auto initialStage =
                validateImageShaderStage(image.name, image.initialState, image.initialShaderStage);
            if (!initialStage) {
                return std::unexpected{std::move(initialStage.error())};
            }
            auto finalStage =
                validateImageShaderStage(image.name, image.finalState, image.finalShaderStage);
            if (!finalStage) {
                return std::unexpected{std::move(finalStage.error())};
            }
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
            auto initialStage = validateBufferShaderStage(buffer.name, buffer.initialState,
                                                          buffer.initialShaderStage);
            if (!initialStage) {
                return std::unexpected{std::move(initialStage.error())};
            }
            auto finalStage =
                validateBufferShaderStage(buffer.name, buffer.finalState, buffer.finalShaderStage);
            if (!finalStage) {
                return std::unexpected{std::move(finalStage.error())};
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
