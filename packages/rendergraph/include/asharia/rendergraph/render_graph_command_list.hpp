#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia {

    class RenderGraphCommandList {
    public:
        RenderGraphCommandList& setShader(std::string shaderAsset, std::string shaderPass);
        RenderGraphCommandList& setTexture(std::string bindingName, std::string slotName);
        RenderGraphCommandList& setFloat(std::string bindingName, float value);
        RenderGraphCommandList& setInt(std::string bindingName, int value);
        RenderGraphCommandList& setVec4(std::string bindingName, std::array<float, 4> value);
        RenderGraphCommandList& drawFullscreenTriangle();
        RenderGraphCommandList& clearColor(std::string slotName, std::array<float, 4> color);
        RenderGraphCommandList& copyImage(std::string sourceSlotName, std::string targetSlotName);
        RenderGraphCommandList& dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                                         std::uint32_t groupCountZ);

        [[nodiscard]] std::span<const RenderGraphCommand> commands() const;
        [[nodiscard]] std::vector<RenderGraphCommand> takeCommands() &&;

    private:
        std::vector<RenderGraphCommand> commands_;
    };

} // namespace asharia
