#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace asharia::asset_processor {

    struct ProductExecutionOptions {
        std::filesystem::path sourceRoot;
        std::string sourcePathPrefix;
        std::string targetProfile;
        std::filesystem::path outputRoot;
        std::optional<std::filesystem::path> productManifestPath;
        std::filesystem::path productManifestOutputPath;
        std::vector<std::string> ignoredDirectoryNames;
    };

    struct ProductExecution {
        int exitCode{};
        std::string text;
    };

    [[nodiscard]] ProductExecution runProductExecution(const ProductExecutionOptions& options);

} // namespace asharia::asset_processor
