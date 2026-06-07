#include "asset_processor_dry_run.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_pipeline/asset_import_planning.hpp"
#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"
#include "asharia/asset_pipeline/asset_scanned_import_planning.hpp"
#include "asharia/asset_pipeline/asset_source_scan.hpp"

#include "asset_processor_project_input.hpp"
#include "asset_processor_text.hpp"

namespace asharia::asset_processor {
    namespace {

        struct ManifestLoadResult {
            bool succeeded{};
            asharia::asset::AssetProductManifestDocument manifest;
            std::string error;
        };

        [[nodiscard]] std::string toText(asharia::asset::AssetSourceScanDiagnosticCode code) {
            using Code = asharia::asset::AssetSourceScanDiagnosticCode;
            switch (code) {
            case Code::InvalidRequest:
                return "InvalidRequest";
            case Code::InvalidRoot:
                return "InvalidRoot";
            case Code::FilesystemError:
                return "FilesystemError";
            case Code::InvalidSourcePath:
                return "InvalidSourcePath";
            case Code::DuplicateSourcePath:
                return "DuplicateSourcePath";
            case Code::DuplicateMetadataPath:
                return "DuplicateMetadataPath";
            case Code::MissingMetadata:
                return "MissingMetadata";
            case Code::OrphanMetadata:
                return "OrphanMetadata";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string toText(asharia::asset::AssetSourceDiscoveryDiagnosticCode code) {
            using Code = asharia::asset::AssetSourceDiscoveryDiagnosticCode;
            switch (code) {
            case Code::InvalidEntry:
                return "InvalidEntry";
            case Code::MissingMetadata:
                return "MissingMetadata";
            case Code::MetadataReadFailed:
                return "MetadataReadFailed";
            case Code::SourcePathMismatch:
                return "SourcePathMismatch";
            case Code::DuplicateGuid:
                return "DuplicateGuid";
            case Code::DuplicateSourcePath:
                return "DuplicateSourcePath";
            case Code::CatalogRejected:
                return "CatalogRejected";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string toText(asharia::asset::AssetSourceSnapshotDiagnosticCode code) {
            using Code = asharia::asset::AssetSourceSnapshotDiagnosticCode;
            switch (code) {
            case Code::InvalidEntry:
                return "InvalidEntry";
            case Code::MissingSourceFile:
                return "MissingSourceFile";
            case Code::SourceFileNotRegular:
                return "SourceFileNotRegular";
            case Code::SourceFileReadFailed:
                return "SourceFileReadFailed";
            case Code::DuplicateSourcePath:
                return "DuplicateSourcePath";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string toText(asharia::asset::AssetImportPlanDiagnosticCode code) {
            using Code = asharia::asset::AssetImportPlanDiagnosticCode;
            switch (code) {
            case Code::InvalidTargetProfile:
                return "InvalidTargetProfile";
            case Code::InvalidSource:
                return "InvalidSource";
            case Code::InvalidSourceSnapshot:
                return "InvalidSourceSnapshot";
            case Code::MissingSourceSnapshot:
                return "MissingSourceSnapshot";
            case Code::DuplicateSource:
                return "DuplicateSource";
            case Code::DuplicateSourceSnapshot:
                return "DuplicateSourceSnapshot";
            case Code::InvalidProductManifest:
                return "InvalidProductManifest";
            case Code::MetadataSourceHashDrift:
                return "MetadataSourceHashDrift";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string
        toText(asharia::asset::AssetImportPlanDiagnosticSeverity severity) {
            using Severity = asharia::asset::AssetImportPlanDiagnosticSeverity;
            switch (severity) {
            case Severity::Info:
                return "Info";
            case Severity::Warning:
                return "Warning";
            case Severity::Error:
                return "Error";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string toText(asharia::asset::AssetImportRequestReason reason) {
            using Reason = asharia::asset::AssetImportRequestReason;
            switch (reason) {
            case Reason::MissingProduct:
                return "MissingProduct";
            case Reason::SourceChanged:
                return "SourceChanged";
            case Reason::SettingsChanged:
                return "SettingsChanged";
            case Reason::ImporterChanged:
                return "ImporterChanged";
            case Reason::AssetTypeChanged:
                return "AssetTypeChanged";
            case Reason::DependencyChanged:
                return "DependencyChanged";
            case Reason::TargetProfileChanged:
                return "TargetProfileChanged";
            }
            return "Unknown";
        }

        [[nodiscard]] ManifestLoadResult
        loadProductManifest(const std::optional<std::filesystem::path>& manifestPath) {
            if (!manifestPath || manifestPath->empty()) {
                return ManifestLoadResult{
                    .succeeded = true,
                    .manifest = {},
                    .error = {},
                };
            }

            auto manifest = asharia::asset::readAssetProductManifestFile(*manifestPath);
            if (!manifest) {
                return ManifestLoadResult{
                    .succeeded = false,
                    .manifest = {},
                    .error = "Failed to read product manifest path=" + quotePath(*manifestPath) +
                             ": " + manifest.error().message,
                };
            }

            return ManifestLoadResult{
                .succeeded = true,
                .manifest = std::move(*manifest),
                .error = {},
            };
        }

        void appendSourceRoots(std::ostream& output,
                               std::span<const AssetProcessorSourceRoot> sourceRoots) {
            output << "sourceRoots=" << sourceRoots.size() << '\n';
            for (const AssetProcessorSourceRoot& root : sourceRoots) {
                output << "source-root"
                       << " rootName=" << quoteText(root.rootName)
                       << " sourceRoot=" << quotePath(root.sourceRoot);
                if (!root.directory.empty()) {
                    output << " directory=" << quoteText(root.directory);
                }
                output << " sourcePathPrefix=" << quoteText(root.sourcePathPrefix) << '\n';
            }
        }

        void appendIgnoredDirectories(std::ostream& output,
                                      std::span<const std::string> ignoredDirectoryNames) {
            output << "ignoredDirectories=" << ignoredDirectoryNames.size();
            for (const std::string& ignoredDirectoryName : ignoredDirectoryNames) {
                output << " " << quoteText(ignoredDirectoryName);
            }
            output << '\n';
        }

        void appendScanDiagnostics(std::ostream& output,
                                   const asharia::asset::AssetSourceScanResult& scan) {
            for (const asharia::asset::AssetSourceScanDiagnostic& diagnostic : scan.diagnostics) {
                output << "diagnostic stage=scan"
                       << " code=" << toText(diagnostic.code)
                       << " source=" << quoteText(diagnostic.sourcePath)
                       << " file=" << quotePath(diagnostic.sourceFilePath)
                       << " metadata=" << quotePath(diagnostic.metadataPath)
                       << " message=" << quoteText(diagnostic.message) << '\n';
            }
        }

        void
        appendDiscoveryDiagnostics(std::ostream& output,
                                   const asharia::asset::AssetSourceDiscoveryResult& discovery) {
            for (const asharia::asset::AssetSourceDiscoveryDiagnostic& diagnostic :
                 discovery.diagnostics) {
                output << "diagnostic stage=discovery"
                       << " code=" << toText(diagnostic.code)
                       << " source=" << quoteText(diagnostic.sourcePath)
                       << " metadata=" << quotePath(diagnostic.metadataPath)
                       << " message=" << quoteText(diagnostic.message) << '\n';
            }
        }

        void appendSnapshotDiagnostics(std::ostream& output,
                                       const asharia::asset::AssetSourceSnapshotResult& snapshot) {
            for (const asharia::asset::AssetSourceSnapshotDiagnostic& diagnostic :
                 snapshot.diagnostics) {
                output << "diagnostic stage=snapshot"
                       << " code=" << toText(diagnostic.code)
                       << " source=" << quoteText(diagnostic.sourcePath)
                       << " file=" << quotePath(diagnostic.sourceFilePath)
                       << " message=" << quoteText(diagnostic.message) << '\n';
            }
        }

        void appendPlanDiagnostics(std::ostream& output,
                                   const asharia::asset::AssetImportPlanResult& plan) {
            for (const asharia::asset::AssetImportPlanDiagnostic& diagnostic : plan.diagnostics) {
                output << "diagnostic stage=planning"
                       << " severity=" << toText(diagnostic.severity)
                       << " code=" << toText(diagnostic.code)
                       << " source=" << quoteText(diagnostic.sourcePath)
                       << " message=" << quoteText(diagnostic.message) << '\n';
            }
        }

        void appendImportRequests(std::ostream& output,
                                  const asharia::asset::AssetImportPlanResult& plan) {
            for (const asharia::asset::AssetImportRequest& request : plan.requests) {
                output << "import-request"
                       << " source=" << quoteText(request.source.sourcePath) << " guid="
                       << quoteText(asharia::asset::formatAssetGuid(request.source.guid))
                       << " assetType=" << quoteText(request.source.assetTypeName)
                       << " importer=" << quoteText(request.source.importerName)
                       << " reason=" << toText(request.reason)
                       << " productPath=" << quoteText(request.relativeProductPath)
                       << " sourceHash=" << formatHash64(request.source.sourceHash)
                       << " settingsHash=" << formatHash64(request.source.settingsHash)
                       << " dependencyHash=" << formatHash64(request.productKey.dependencyHash)
                       << '\n';
            }
        }

        void appendCacheHits(std::ostream& output,
                             const asharia::asset::AssetImportPlanResult& plan) {
            for (const asharia::asset::AssetImportCacheHit& hit : plan.cacheHits) {
                output << "cache-hit"
                       << " source=" << quoteText(hit.source.sourcePath)
                       << " guid=" << quoteText(asharia::asset::formatAssetGuid(hit.source.guid))
                       << " assetType=" << quoteText(hit.source.assetTypeName)
                       << " importer=" << quoteText(hit.source.importerName)
                       << " productPath=" << quoteText(hit.product.relativeProductPath)
                       << " productHash=" << formatHash64(hit.product.productHash) << '\n';
            }
        }

    } // namespace

    DryRunExecution runDryRun(const DryRunOptions& options) {
        std::ostringstream output;
        output << "asset-processor dry-run\n";
        if (options.projectPath) {
            output << "projectPath=" << quotePath(*options.projectPath) << '\n';
        } else {
            output << "sourceRoot=" << quotePath(options.sourceRoot) << '\n'
                   << "sourcePathPrefix=" << quoteText(options.sourcePathPrefix) << '\n';
        }
        output << "targetProfile=" << quoteText(options.targetProfile) << '\n'
               << "productManifest="
               << (options.productManifestPath ? quotePath(*options.productManifestPath)
                                               : quoteText("<empty>"))
               << '\n';

        const AssetProcessorResolvedInput input =
            resolveAssetProcessorInput(AssetProcessorInputOptions{
                .projectPath = options.projectPath,
                .sourceRoot = options.sourceRoot,
                .sourcePathPrefix = options.sourcePathPrefix,
                .ignoredDirectoryNames = options.ignoredDirectoryNames,
            });
        if (!input.succeeded) {
            output << "diagnostic stage=project"
                   << " code=ReadFailed"
                   << " message=" << quoteText(input.error) << '\n';
            return DryRunExecution{
                .exitCode = EXIT_FAILURE,
                .text = output.str(),
            };
        }

        if (input.projectPath) {
            output << "projectName=" << quoteText(input.projectName) << '\n'
                   << "projectId=" << quoteText(input.projectId) << '\n'
                   << "assetCacheRoot=" << quoteText(input.assetCacheRoot) << '\n';
        }
        appendIgnoredDirectories(output, input.ignoredDirectoryNames);
        appendSourceRoots(output, input.sourceRoots);

        ManifestLoadResult manifest = loadProductManifest(options.productManifestPath);
        if (!manifest.succeeded) {
            output << "diagnostic stage=product-manifest"
                   << " code=ReadFailed"
                   << " message=" << quoteText(manifest.error) << '\n';
            return DryRunExecution{
                .exitCode = EXIT_FAILURE,
                .text = output.str(),
            };
        }

        const asharia::asset::AssetScannedImportPlanResult result =
            planAssetProcessorImports(input, manifest.manifest, options.targetProfile);

        output << "scan entries=" << result.scan.entries.size()
               << " diagnostics=" << result.scan.diagnostics.size() << '\n'
               << "discovery records=" << result.discovery.manifest.records.size()
               << " diagnostics=" << result.discovery.diagnostics.size() << '\n'
               << "snapshot snapshots=" << result.snapshot.snapshots.size()
               << " diagnostics=" << result.snapshot.diagnostics.size() << '\n'
               << "planning requests=" << result.plan.requests.size()
               << " cacheHits=" << result.plan.cacheHits.size()
               << " diagnostics=" << result.plan.diagnostics.size() << '\n';

        appendImportRequests(output, result.plan);
        appendCacheHits(output, result.plan);
        appendScanDiagnostics(output, result.scan);
        appendDiscoveryDiagnostics(output, result.discovery);
        appendSnapshotDiagnostics(output, result.snapshot);
        appendPlanDiagnostics(output, result.plan);

        return DryRunExecution{
            .exitCode = result.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE,
            .text = output.str(),
        };
    }

} // namespace asharia::asset_processor
