#include <utility>

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
        const auto vertices = graph.importBuffer(RenderGraphBufferDesc{
            .name = "BuilderHeaderVertices",
            .byteSize = 12,
            .initialState = RenderGraphBufferState::VertexRead,
            .finalState = RenderGraphBufferState::VertexRead,
        });
        const auto indices = graph.importBuffer(RenderGraphBufferDesc{
            .name = "BuilderHeaderIndices",
            .byteSize = 12,
            .initialState = RenderGraphBufferState::IndexRead,
            .finalState = RenderGraphBufferState::IndexRead,
        });

        graph.addPass("BuilderHeaderPass", "header.builder")
            .writeColor("target", image)
            .readVertexBuffer("vertices", vertices)
            .readIndexBuffer("indices", indices)
            .setParams("header.builder.params", BuilderHeaderParams{.value = 1.0F})
            .recordCommands([](RenderGraphCommandList& commands) {
                commands.setFloat("Value", 1.0F).drawFullscreenTriangle();
            });

        RenderGraph copyConstructed{graph};
        RenderGraph copyAssigned;
        copyAssigned = copyConstructed;
        RenderGraph moveConstructed{std::move(copyAssigned)};
        RenderGraph moveAssigned;
        moveAssigned = std::move(moveConstructed);
        [[maybe_unused]] const Result<RenderGraphCompileResult> compiled = moveAssigned.compile();
    }

} // namespace asharia::rendergraph_header_tests
