#include "render_graph_command_schema_validation.hpp"

#include <algorithm>
#include <string>

#include "asharia/rendergraph/render_graph_types.hpp"

#include "render_graph_debug_names.hpp"
#include "render_graph_pass.hpp"

namespace asharia {

    namespace {

        [[nodiscard]] bool commandAllowedBySchema(RenderGraphCommandKind commandKind,
                                                  const RenderGraphPassSchema& schema) {
            return std::ranges::any_of(
                schema.allowedCommands,
                [commandKind](RenderGraphCommandKind allowed) { return allowed == commandKind; });
        }

    } // namespace

    namespace rendergraph_internal {

        Result<void> validateCommandsAgainstSchema(const Pass& pass,
                                                   const RenderGraphPassSchema& schema) {
            for (const RenderGraphCommand& command : pass.commands) {
                if (!commandAllowedBySchema(command.kind, schema)) {
                    return std::unexpected{Error{
                        ErrorDomain::RenderGraph,
                        0,
                        "Render graph pass '" + pass.name + "' command '" +
                            std::string{commandKindName(command.kind)} +
                            "' is not allowed by schema '" + schema.type + "'.",
                    }};
                }
            }

            return {};
        }

    } // namespace rendergraph_internal

} // namespace asharia
