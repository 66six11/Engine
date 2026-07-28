#include "asharia/project_bootstrap/project_bootstrap_reader.hpp"

#include <expected>
#include <string>
#include <utility>

#include "asharia/core/error.hpp"
#include "asharia/project/project_descriptor.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace asharia::project_bootstrap {

    Result<ProjectBootstrapSummaryV1>
    readProjectBootstrapSummaryV1(const std::filesystem::path& projectRoot) {
        if (projectRoot.empty()) {
            return std::unexpected{Error{
                ErrorDomain::Project,
                3,
                "Project Bootstrap requires a non-empty project root.",
            }};
        }

        const std::filesystem::path descriptorPath =
            projectRoot / std::string{project::kDefaultAshariaProjectFileName};
        auto descriptor = project::readAshariaProjectDescriptorFile(descriptorPath);
        if (!descriptor) {
            return std::unexpected{std::move(descriptor.error())};
        }

        return ProjectBootstrapSummaryV1{
            .projectName = std::move(descriptor->projectName),
            .projectId = project::formatProjectId(descriptor->projectId),
            .assetSourceRootCount = descriptor->assetSourceRoots.size(),
        };
    }

} // namespace asharia::project_bootstrap
