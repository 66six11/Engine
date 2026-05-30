#include "asharia/rendergraph/render_graph_builder.hpp"

namespace asharia::rendergraph_header_tests {

    struct BuilderHeaderParams {
        float value{};
    };

    void touchBuilderHeader() {
        RenderGraph graph;
        const auto image = graph.createTransientImage(RenderGraphImageDesc{
            .name = "BuilderHeaderImage",
            .format = RenderGraphImageFormat::B8G8R8A8Srgb,
            .extent = RenderGraphExtent2D{.width = 1, .height = 1},
        });

        graph.addPass("BuilderHeaderPass", "header.builder")
            .writeColor("target", image)
            .setParams("header.builder.params", BuilderHeaderParams{.value = 1.0F})
            .recordCommands([](RenderGraphCommandList& commands) {
                commands.setFloat("Value", 1.0F).drawFullscreenTriangle();
            });
    }

} // namespace asharia::rendergraph_header_tests
