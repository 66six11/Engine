#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "asharia/core/result.hpp"

namespace asharia::project_bootstrap {

    struct ProjectBootstrapSummaryV1 final {
        std::string projectName;
        std::string projectId;
        std::size_t assetSourceRootCount{};

        [[nodiscard]] friend bool operator==(const ProjectBootstrapSummaryV1&,
                                             const ProjectBootstrapSummaryV1&) = default;
    };

    [[nodiscard]] Result<ProjectBootstrapSummaryV1>
    readProjectBootstrapSummaryV1(const std::filesystem::path& projectRoot);

    [[nodiscard]] Result<std::string>
    renderProjectBootstrapSummaryJsonV1(const ProjectBootstrapSummaryV1& summary);

} // namespace asharia::project_bootstrap
