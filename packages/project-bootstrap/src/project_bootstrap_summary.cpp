#include <cstdint>
#include <expected>
#include <limits>
#include <string>

#include "asharia/archive/archive_value.hpp"
#include "asharia/archive/json_archive.hpp"
#include "asharia/core/error.hpp"
#include "asharia/project_bootstrap/project_bootstrap_reader.hpp"

namespace asharia::project_bootstrap {

    Result<std::string>
    renderProjectBootstrapSummaryJsonV1(const ProjectBootstrapSummaryV1& summary) {
        if (summary.assetSourceRootCount >
            static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::unexpected{Error{
                ErrorDomain::Project,
                4,
                "Project Bootstrap asset source root count is outside the JSON integer range.",
            }};
        }

        auto rendered = archive::writeJsonArchive(archive::ArchiveValue::object({
            archive::ArchiveMember{
                .key = "projectName",
                .value = archive::ArchiveValue::string(summary.projectName),
            },
            archive::ArchiveMember{
                .key = "projectId",
                .value = archive::ArchiveValue::string(summary.projectId),
            },
            archive::ArchiveMember{
                .key = "assetSourceRootCount",
                .value = archive::ArchiveValue::integer(
                    static_cast<std::int64_t>(summary.assetSourceRootCount)),
            },
        }));
        if (!rendered) {
            return std::unexpected{Error{
                ErrorDomain::Project,
                4,
                "Failed to render Project Bootstrap summary: " + rendered.error().message,
            }};
        }
        return rendered;
    }

} // namespace asharia::project_bootstrap
