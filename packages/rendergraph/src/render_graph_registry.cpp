#include <string>
#include <string_view>
#include <utility>

#include "asharia/rendergraph/render_graph_execution.hpp"

namespace asharia {
    RenderGraphSchemaRegistry&
    RenderGraphSchemaRegistry::registerSchema(RenderGraphPassSchema schema) {
        for (RenderGraphPassSchema& registered : schemas_) {
            if (registered.type == schema.type) {
                registered = std::move(schema);
                return *this;
            }
        }

        schemas_.push_back(std::move(schema));
        return *this;
    }

    const RenderGraphPassSchema* RenderGraphSchemaRegistry::find(std::string_view type) const {
        for (const RenderGraphPassSchema& schema : schemas_) {
            if (schema.type == type) {
                return &schema;
            }
        }

        return nullptr;
    }

    RenderGraphExecutorRegistry&
    RenderGraphExecutorRegistry::registerExecutor(std::string type,
                                                  RenderGraphPassCallback callback) {
        for (Executor& executor : executors_) {
            if (executor.type == type) {
                executor.callback = std::move(callback);
                return *this;
            }
        }

        executors_.push_back(Executor{
            .type = std::move(type),
            .callback = std::move(callback),
        });
        return *this;
    }

    const RenderGraphPassCallback* RenderGraphExecutorRegistry::find(std::string_view type) const {
        for (const Executor& executor : executors_) {
            if (executor.type == type) {
                return &executor.callback;
            }
        }

        return nullptr;
    }

} // namespace asharia
