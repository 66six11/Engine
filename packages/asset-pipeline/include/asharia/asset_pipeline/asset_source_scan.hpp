#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace asharia::asset {

    inline constexpr std::string_view kAssetMetadataSidecarSuffix = ".ameta";

    enum class AssetSourceScanDiagnosticCode {
        InvalidRequest,
        InvalidRoot,
        FilesystemError,
        InvalidSourcePath,
        DuplicateSourcePath,
        DuplicateMetadataPath,
        MissingMetadata,
        OrphanMetadata,
    };

    struct AssetSourceScanRequest {
        std::filesystem::path sourceRoot;
        std::string sourcePathPrefix;
        std::string metadataSuffix{std::string{kAssetMetadataSidecarSuffix}};
        std::vector<std::string> ignoredDirectoryNames;

        [[nodiscard]] friend bool operator==(const AssetSourceScanRequest&,
                                             const AssetSourceScanRequest&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return !sourceRoot.empty();
        }
    };

    struct AssetSourceScanEntry {
        std::string sourcePath;
        std::filesystem::path sourceFilePath;
        std::filesystem::path metadataPath;

        [[nodiscard]] friend bool operator==(const AssetSourceScanEntry&,
                                             const AssetSourceScanEntry&) = default;
    };

    struct AssetSourceScanDiagnostic {
        AssetSourceScanDiagnosticCode code{AssetSourceScanDiagnosticCode::InvalidRequest};
        std::string sourcePath;
        std::filesystem::path sourceFilePath;
        std::filesystem::path metadataPath;
        std::string message;

        [[nodiscard]] friend bool operator==(const AssetSourceScanDiagnostic&,
                                             const AssetSourceScanDiagnostic&) = default;
    };

    struct AssetSourceScanResult {
        std::vector<AssetSourceScanEntry> entries;
        std::vector<AssetSourceScanDiagnostic> diagnostics;

        [[nodiscard]] friend bool operator==(const AssetSourceScanResult&,
                                             const AssetSourceScanResult&) = default;
        [[nodiscard]] bool succeeded() const noexcept {
            return diagnostics.empty();
        }
    };

    [[nodiscard]] AssetSourceScanResult scanAssetSourceTree(const AssetSourceScanRequest& request);

} // namespace asharia::asset
