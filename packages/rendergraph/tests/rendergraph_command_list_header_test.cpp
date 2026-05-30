#include "asharia/rendergraph/render_graph_command_list.hpp"

namespace asharia::rendergraph_header_tests {

    void touchCommandListHeader() {
        RenderGraphCommandList commands;
        commands.setShader("HeaderShader", "Fullscreen")
            .setTexture("sourceTexture", "source")
            .setVec4("tint", {1.0F, 0.5F, 0.25F, 1.0F})
            .drawFullscreenTriangle();

        (void)commands.commands();
    }

} // namespace asharia::rendergraph_header_tests
