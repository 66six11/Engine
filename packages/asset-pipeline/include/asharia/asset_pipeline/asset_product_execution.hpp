#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "asharia/asset_pipeline/asset_import_planning.hpp"

namespace asharia::asset {

    enum class AssetProductExecutionDiagnosticCode {
        InvalidPlan,
        InvalidProductManifest,
        InvalidSourceBytes,
        MissingSourceBytes,
        DuplicateSourceBytes,
        SourceBytesHashMismatch,
        InvalidOutputRoot,
        InvalidProductPath,
        ProductWriteFailed,
        ManifestWriteFailed,
    };

    struct AssetProductSourceBytes {
        std::string sourcePath;
        std::vector<std::uint8_t> bytes;

        [[nodiscard]] friend bool operator==(const AssetProductSourceBytes&,
                                             const AssetProductSourceBytes&) = default;
    };

    struct AssetProductExecutionDiagnostic {
        AssetProductExecutionDiagnosticCode code{AssetProductExecutionDiagnosticCode::InvalidPlan};
        std::string sourcePath;
        std::string relativeProductPath;
        std::string message;

        [[nodiscard]] friend bool operator==(const AssetProductExecutionDiagnostic&,
                                             const AssetProductExecutionDiagnostic&) = default;
    };

    struct AssetProductWrite {
        SourceAssetRecord source;
        AssetProductRecord product;
        std::filesystem::path productFilePath;

        [[nodiscard]] friend bool operator==(const AssetProductWrite&,
                                             const AssetProductWrite&) = default;
    };

    struct AssetProductExecutionRequest {
        AssetImportPlanResult plan;
        AssetProductManifestDocument existingManifest;
        std::vector<AssetProductSourceBytes> sourceBytes;
        std::filesystem::path productOutputRoot;
        std::filesystem::path productManifestOutputPath;
    };

    struct AssetProductExecutionResult {
        std::string targetProfile;
        std::uint64_t targetProfileHash{};
        std::vector<AssetProductWrite> writtenProducts;
        std::vector<AssetImportCacheHit> cacheHits;
        AssetProductManifestDocument manifest;
        std::vector<AssetProductExecutionDiagnostic> diagnostics;
        bool manifestWritten{};

        [[nodiscard]] bool succeeded() const noexcept {
            return diagnostics.empty();
        }
    };

    [[nodiscard]] AssetProductExecutionResult
    executeAssetProducts(const AssetProductExecutionRequest& request);

} // namespace asharia::asset
