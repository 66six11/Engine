#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace asharia::asset_processor {

    struct DryRunOptions {
        std::optional<std::filesystem::path> projectPath;
        std::filesystem::path sourceRoot;
        std::string sourcePathPrefix;
        std::string targetProfile;
        std::optional<std::filesystem::path> productManifestPath;
        std::vector<std::string> ignoredDirectoryNames;
    };

    struct DryRunExecution {
        int exitCode{};
        std::string text;
    };

    [[nodiscard]] DryRunExecution runDryRun(const DryRunOptions& options);

} // namespace asharia::asset_processor
