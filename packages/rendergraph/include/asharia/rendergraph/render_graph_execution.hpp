#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "asharia/rendergraph/render_graph_pass_context.hpp"
#include "asharia/rendergraph/render_graph_types.hpp"

namespace asharia {

    class RenderGraphSchemaRegistry {
    public:
        RenderGraphSchemaRegistry& registerSchema(RenderGraphPassSchema schema);
        [[nodiscard]] const RenderGraphPassSchema* find(std::string_view type) const;

    private:
        std::vector<RenderGraphPassSchema> schemas_;
    };

    class RenderGraphExecutorRegistry {
    public:
        RenderGraphExecutorRegistry& registerExecutor(std::string type,
                                                      RenderGraphPassCallback callback);
        [[nodiscard]] const RenderGraphPassCallback* find(std::string_view type) const;

    private:
        struct Executor {
            std::string type;
            RenderGraphPassCallback callback;
        };

        std::vector<Executor> executors_;
    };

} // namespace asharia
