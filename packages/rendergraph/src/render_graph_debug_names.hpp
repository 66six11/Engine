#pragma once

#include <string>
#include <string_view>

#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia::rendergraph_internal {

    [[nodiscard]] std::string_view imageFormatName(RenderGraphImageFormat format);

    [[nodiscard]] std::string_view imageStateName(RenderGraphImageState state);

    [[nodiscard]] std::string_view bufferStateName(RenderGraphBufferState state);

    [[nodiscard]] std::string_view imageLifetimeName(RenderGraphImageLifetime lifetime);

    [[nodiscard]] std::string_view bufferLifetimeName(RenderGraphBufferLifetime lifetime);

    [[nodiscard]] std::string_view shaderStageName(RenderGraphShaderStage stage);

    [[nodiscard]] std::string_view commandKindName(RenderGraphCommandKind kind);

    [[nodiscard]] std::string commandDetail(const RenderGraphCommand& command);

    [[nodiscard]] std::string imageAccessName(RenderGraphImageState state,
                                              RenderGraphShaderStage shaderStage);

    [[nodiscard]] std::string bufferAccessName(RenderGraphBufferState state,
                                               RenderGraphShaderStage shaderStage);

} // namespace asharia::rendergraph_internal
