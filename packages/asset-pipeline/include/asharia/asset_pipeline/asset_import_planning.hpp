#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_source_discovery.hpp"
#include "asharia/asset_pipeline/asset_source_snapshot.hpp"
#include "asharia/asset_pipeline/asset_tool_fingerprint.hpp"

namespace asharia::asset {

    enum class AssetImportRequestReason {
        MissingProduct,
        SourceChanged,
        SettingsChanged,
        ImporterChanged,
        AssetTypeChanged,
        DependencyChanged,
        TargetProfileChanged,
    };

    enum class AssetImportPlanDiagnosticSeverity : std::uint8_t {
        Info,
        Warning,
        Error,
    };

    enum class AssetImportPlanDiagnosticCode {
        InvalidTargetProfile,
        InvalidSource,
        InvalidSourceSnapshot,
        MissingSourceSnapshot,
        DuplicateSource,
        DuplicateSourceSnapshot,
        InvalidProductManifest,
        MetadataSourceHashDrift,
        ToolFingerprintFailed,
    };

    struct AssetImportToolVersionDependency {
        ImporterId importerId{};
        std::string toolName;
        std::uint64_t versionHash{};

        [[nodiscard]] friend bool operator==(const AssetImportToolVersionDependency&,
                                             const AssetImportToolVersionDependency&) = default;
    };

    using AssetToolFingerprintResolver =
        Result<AssetToolFingerprint> (*)(std::string_view logicalToolName);

    struct AssetImportPlanOptions {
        std::vector<AssetImportToolVersionDependency> toolVersions;
        AssetToolFingerprintResolver toolFingerprintResolver{};

        [[nodiscard]] friend bool operator==(const AssetImportPlanOptions&,
                                             const AssetImportPlanOptions&) = default;
    };

    struct AssetImportRequest {
        SourceAssetRecord source;
        std::vector<AssetImportSetting> settings;
        std::vector<AssetDependency> dependencies;
        AssetProductKey productKey{};
        std::string relativeProductPath;
        AssetImportRequestReason reason{AssetImportRequestReason::MissingProduct};

        [[nodiscard]] friend bool operator==(const AssetImportRequest&,
                                             const AssetImportRequest&) = default;
    };

    struct AssetImportCacheHit {
        SourceAssetRecord source;
        std::vector<AssetDependency> dependencies;
        AssetProductRecord product;

        [[nodiscard]] friend bool operator==(const AssetImportCacheHit&,
                                             const AssetImportCacheHit&) = default;
    };

    struct AssetImportPlanDiagnostic {
        AssetImportPlanDiagnosticCode code{AssetImportPlanDiagnosticCode::InvalidSource};
        AssetImportPlanDiagnosticSeverity severity{AssetImportPlanDiagnosticSeverity::Error};
        std::string sourcePath;
        std::string message;

        [[nodiscard]] friend bool operator==(const AssetImportPlanDiagnostic&,
                                             const AssetImportPlanDiagnostic&) = default;
    };

    struct AssetImportPlanResult {
        std::string targetProfile;
        std::uint64_t targetProfileHash{};
        std::vector<AssetImportRequest> requests;
        std::vector<AssetImportCacheHit> cacheHits;
        std::vector<AssetImportPlanDiagnostic> diagnostics;

        [[nodiscard]] friend bool operator==(const AssetImportPlanResult&,
                                             const AssetImportPlanResult&) = default;
        [[nodiscard]] bool succeeded() const noexcept {
            return std::ranges::none_of(
                diagnostics, [](const AssetImportPlanDiagnostic& diagnostic) {
                    return diagnostic.severity == AssetImportPlanDiagnosticSeverity::Error;
                });
        }
    };

    [[nodiscard]] std::string makeAssetImportProductPath(const AssetProductKey& productKey,
                                                         std::string_view targetProfile);

    [[nodiscard]] AssetImportPlanResult
    planAssetImports(std::span<const DiscoveredSourceAsset> sources,
                     std::span<const AssetSourceSnapshot> snapshots,
                     const AssetProductManifestDocument& productManifest,
                     std::string_view targetProfile, const AssetImportPlanOptions& options = {});

} // namespace asharia::asset
