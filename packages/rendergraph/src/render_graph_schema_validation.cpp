#include "render_graph_schema_validation.hpp"

#include <string>
#include <utility>

#include "asharia/rendergraph/render_graph_execution.hpp"

#include "render_graph_command_schema_validation.hpp"
#include "render_graph_pass.hpp"
#include "render_graph_resource_schema_validation.hpp"

namespace asharia::rendergraph_internal {

    Result<void> validatePassSchema(const Pass& pass,
                                    const RenderGraphSchemaRegistry& schemaRegistry) {
        if (pass.type.empty()) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph pass '" + pass.name + "' cannot be schema-validated without a type.",
            }};
        }

        const RenderGraphPassSchema* schema = schemaRegistry.find(pass.type);
        if (schema == nullptr) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph pass '" + pass.name + "' has no registered schema for type '" +
                    pass.type + "'.",
            }};
        }

        if (pass.paramsType != schema->paramsType) {
            return std::unexpected{Error{
                ErrorDomain::RenderGraph,
                0,
                "Render graph pass '" + pass.name + "' expected params type '" +
                    schema->paramsType + "' but found '" + pass.paramsType + "'.",
            }};
        }

        auto resources = validateResourceSlotsAgainstSchema(pass, *schema);
        if (!resources) {
            return std::unexpected{std::move(resources.error())};
        }

        auto commands = validateCommandsAgainstSchema(pass, *schema);
        if (!commands) {
            return std::unexpected{std::move(commands.error())};
        }

        return {};
    }

} // namespace asharia::rendergraph_internal
