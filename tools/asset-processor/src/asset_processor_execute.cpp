#include "asset_processor_execute.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_pipeline/asset_product_execution.hpp"
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

        struct ProductExecutionInput {
            bool succeeded{};
            AssetProcessorResolvedInput sources;
            std::filesystem::path outputRoot;
            std::optional<std::filesystem::path> productManifestPath;
            std::filesystem::path productManifestOutputPath;
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

        [[nodiscard]] std::string toText(asharia::asset::AssetProductExecutionDiagnosticCode code) {
            using Code = asharia::asset::AssetProductExecutionDiagnosticCode;
            switch (code) {
            case Code::InvalidPlan:
                return "InvalidPlan";
            case Code::InvalidProductManifest:
                return "InvalidProductManifest";
            case Code::InvalidSourceBytes:
                return "InvalidSourceBytes";
            case Code::MissingSourceBytes:
                return "MissingSourceBytes";
            case Code::DuplicateSourceBytes:
                return "DuplicateSourceBytes";
            case Code::SourceBytesHashMismatch:
                return "SourceBytesHashMismatch";
            case Code::InvalidOutputRoot:
                return "InvalidOutputRoot";
            case Code::InvalidProductPath:
                return "InvalidProductPath";
            case Code::ProductWriteFailed:
                return "ProductWriteFailed";
            case Code::ManifestWriteFailed:
                return "ManifestWriteFailed";
            case Code::TextureImportFailed:
                return "TextureImportFailed";
            case Code::MaterialInstanceImportFailed:
                return "MaterialInstanceImportFailed";
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

        [[nodiscard]] std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path,
                                                              bool& succeeded) {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                succeeded = false;
                return {};
            }

            std::vector<std::uint8_t> bytes;
            std::array<char, 4096> buffer{};
            while (file) {
                file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize bytesRead = file.gcount();
                const auto end = buffer.begin() + bytesRead;
                for (auto byte = buffer.begin(); byte != end; ++byte) {
                    bytes.push_back(static_cast<std::uint8_t>(*byte));
                }
            }

            succeeded = !file.bad();
            return bytes;
        }

        [[nodiscard]] std::vector<asharia::asset::AssetProductSourceBytes>
        readSourceBytes(std::ostream& output,
                        std::span<const asharia::asset::AssetSourceScanEntry> entries) {
            std::vector<asharia::asset::AssetProductSourceBytes> sources;
            sources.reserve(entries.size());
            for (const asharia::asset::AssetSourceScanEntry& entry : entries) {
                bool succeeded = false;
                std::vector<std::uint8_t> bytes = readFileBytes(entry.sourceFilePath, succeeded);
                if (!succeeded) {
                    output << "diagnostic stage=source-bytes"
                           << " code=ReadFailed"
                           << " source=" << quoteText(entry.sourcePath)
                           << " file=" << quotePath(entry.sourceFilePath)
                           << " message=" << quoteText("Failed to read explicit source bytes.")
                           << '\n';
                    continue;
                }

                sources.push_back(asharia::asset::AssetProductSourceBytes{
                    .sourcePath = entry.sourcePath,
                    .bytes = std::move(bytes),
                });
            }
            return sources;
        }

        void appendProductWrites(std::ostream& output,
                                 const asharia::asset::AssetProductExecutionResult& execution) {
            for (const asharia::asset::AssetProductWrite& write : execution.writtenProducts) {
                output << "product-written"
                       << " source=" << quoteText(write.source.sourcePath)
                       << " guid=" << quoteText(asharia::asset::formatAssetGuid(write.source.guid))
                       << " productPath=" << quoteText(write.product.relativeProductPath)
                       << " productFile=" << quotePath(write.productFilePath)
                       << " productSizeBytes=" << write.product.productSizeBytes
                       << " productHash=" << formatHash64(write.product.productHash) << '\n';
            }
        }

        void appendCacheHits(std::ostream& output,
                             const asharia::asset::AssetProductExecutionResult& execution) {
            for (const asharia::asset::AssetImportCacheHit& hit : execution.cacheHits) {
                output << "cache-hit"
                       << " source=" << quoteText(hit.source.sourcePath)
                       << " guid=" << quoteText(asharia::asset::formatAssetGuid(hit.source.guid))
                       << " productPath=" << quoteText(hit.product.relativeProductPath)
                       << " productHash=" << formatHash64(hit.product.productHash) << '\n';
            }
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

        void
        appendExecutionDiagnostics(std::ostream& output,
                                   const asharia::asset::AssetProductExecutionResult& execution) {
            for (const asharia::asset::AssetProductExecutionDiagnostic& diagnostic :
                 execution.diagnostics) {
                output << "diagnostic stage=execution"
                       << " code=" << toText(diagnostic.code)
                       << " source=" << quoteText(diagnostic.sourcePath)
                       << " productPath=" << quoteText(diagnostic.relativeProductPath)
                       << " message=" << quoteText(diagnostic.message) << '\n';
            }
        }

        [[nodiscard]] std::filesystem::path
        projectManifestOutputPath(const std::filesystem::path& outputRoot) {
            const std::filesystem::path parent = outputRoot.parent_path();
            if (parent.empty()) {
                return std::filesystem::path{"products.aproducts.json"};
            }
            return parent / "products.aproducts.json";
        }

        [[nodiscard]] std::filesystem::path
        defaultManifestOutputPath(const ProductExecutionOptions& options,
                                  const AssetProcessorResolvedInput& input,
                                  const std::filesystem::path& outputRoot) {
            if (!options.productManifestOutputPath.empty()) {
                return options.productManifestOutputPath;
            }
            if (input.projectPath) {
                return projectManifestOutputPath(outputRoot);
            }
            return outputRoot / "product-manifest.json";
        }

        [[nodiscard]] std::optional<std::filesystem::path>
        defaultExistingProjectManifestPath(const ProductExecutionOptions& options,
                                           const std::filesystem::path& productManifestOutputPath) {
            if (options.productManifestPath) {
                return options.productManifestPath;
            }
            if (!options.projectPath) {
                return std::nullopt;
            }

            std::error_code existsError;
            if (std::filesystem::exists(productManifestOutputPath, existsError) && !existsError) {
                return productManifestOutputPath;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::filesystem::path
        defaultProjectOutputRoot(const AssetProcessorResolvedInput& input) {
            return input.projectRoot / std::filesystem::path{input.assetCacheRoot};
        }

        [[nodiscard]] ProductExecutionInput
        resolveProductExecutionInput(const ProductExecutionOptions& options) {
            AssetProcessorResolvedInput sources =
                resolveAssetProcessorInput(AssetProcessorInputOptions{
                    .projectPath = options.projectPath,
                    .sourceRoot = options.sourceRoot,
                    .sourcePathPrefix = options.sourcePathPrefix,
                    .ignoredDirectoryNames = options.ignoredDirectoryNames,
                });
            if (!sources.succeeded) {
                const std::string error = sources.error;
                return ProductExecutionInput{
                    .succeeded = false,
                    .sources = std::move(sources),
                    .outputRoot = {},
                    .productManifestPath = std::nullopt,
                    .productManifestOutputPath = {},
                    .error = error,
                };
            }

            std::filesystem::path outputRoot = options.outputRoot;
            if (outputRoot.empty() && sources.projectPath) {
                outputRoot = defaultProjectOutputRoot(sources);
            }

            const std::filesystem::path productManifestOutputPath =
                defaultManifestOutputPath(options, sources, outputRoot);
            return ProductExecutionInput{
                .succeeded = true,
                .sources = std::move(sources),
                .outputRoot = outputRoot,
                .productManifestPath =
                    defaultExistingProjectManifestPath(options, productManifestOutputPath),
                .productManifestOutputPath = productManifestOutputPath,
                .error = {},
            };
        }

        void appendIgnoredDirectories(std::ostream& output,
                                      std::span<const std::string> ignoredDirectoryNames) {
            output << "ignoredDirectories=" << ignoredDirectoryNames.size();
            for (const std::string& ignoredDirectoryName : ignoredDirectoryNames) {
                output << " " << quoteText(ignoredDirectoryName);
            }
            output << '\n';
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

    } // namespace

    ProductExecution runProductExecution(const ProductExecutionOptions& options) {
        std::ostringstream output;
        const ProductExecutionInput input = resolveProductExecutionInput(options);

        output << "asset-processor execute\n";
        if (options.projectPath) {
            output << "projectPath=" << quotePath(*options.projectPath) << '\n';
        } else {
            output << "sourceRoot=" << quotePath(options.sourceRoot) << '\n'
                   << "sourcePathPrefix=" << quoteText(options.sourcePathPrefix) << '\n';
        }
        output << "targetProfile=" << quoteText(options.targetProfile) << '\n'
               << "productManifest="
               << (input.productManifestPath ? quotePath(*input.productManifestPath)
                                             : quoteText("<empty>"))
               << '\n'
               << "outputRoot=" << quotePath(input.outputRoot) << '\n'
               << "manifestOutput=" << quotePath(input.productManifestOutputPath) << '\n';

        if (!input.succeeded) {
            output << "diagnostic stage=project"
                   << " code=ReadFailed"
                   << " message=" << quoteText(input.error) << '\n';
            return ProductExecution{
                .exitCode = EXIT_FAILURE,
                .text = output.str(),
            };
        }

        if (input.sources.projectPath) {
            output << "projectName=" << quoteText(input.sources.projectName) << '\n'
                   << "projectId=" << quoteText(input.sources.projectId) << '\n'
                   << "assetCacheRoot=" << quoteText(input.sources.assetCacheRoot) << '\n';
        }
        appendIgnoredDirectories(output, input.sources.ignoredDirectoryNames);
        appendSourceRoots(output, input.sources.sourceRoots);

        ManifestLoadResult manifest = loadProductManifest(input.productManifestPath);
        if (!manifest.succeeded) {
            output << "diagnostic stage=product-manifest"
                   << " code=ReadFailed"
                   << " message=" << quoteText(manifest.error) << '\n';
            return ProductExecution{
                .exitCode = EXIT_FAILURE,
                .text = output.str(),
            };
        }

        asharia::asset::AssetScannedImportPlanResult plan =
            planAssetProcessorImports(input.sources, manifest.manifest, options.targetProfile);

        output << "scan entries=" << plan.scan.entries.size()
               << " diagnostics=" << plan.scan.diagnostics.size() << '\n'
               << "discovery records=" << plan.discovery.manifest.records.size()
               << " diagnostics=" << plan.discovery.diagnostics.size() << '\n'
               << "snapshot snapshots=" << plan.snapshot.snapshots.size()
               << " diagnostics=" << plan.snapshot.diagnostics.size() << '\n'
               << "planning requests=" << plan.plan.requests.size()
               << " cacheHits=" << plan.plan.cacheHits.size()
               << " diagnostics=" << plan.plan.diagnostics.size() << '\n';

        appendPlanDiagnostics(output, plan.plan);
        if (!plan.succeeded()) {
            appendScanDiagnostics(output, plan.scan);
            appendDiscoveryDiagnostics(output, plan.discovery);
            appendSnapshotDiagnostics(output, plan.snapshot);
            return ProductExecution{
                .exitCode = EXIT_FAILURE,
                .text = output.str(),
            };
        }

        std::vector<asharia::asset::AssetProductSourceBytes> sourceBytes =
            readSourceBytes(output, plan.scan.entries);
        if (sourceBytes.size() != plan.scan.entries.size()) {
            return ProductExecution{
                .exitCode = EXIT_FAILURE,
                .text = output.str(),
            };
        }

        const asharia::asset::AssetProductExecutionResult execution =
            asharia::asset::executeAssetProducts(asharia::asset::AssetProductExecutionRequest{
                .plan = std::move(plan.plan),
                .existingManifest = std::move(manifest.manifest),
                .sourceBytes = std::move(sourceBytes),
                .productOutputRoot = input.outputRoot,
                .productManifestOutputPath = input.productManifestOutputPath,
            });

        output << "execution written=" << execution.writtenProducts.size()
               << " cacheHits=" << execution.cacheHits.size()
               << " diagnostics=" << execution.diagnostics.size()
               << " manifestProducts=" << execution.manifest.products.size()
               << " manifestWritten=" << (execution.manifestWritten ? "true" : "false") << '\n';
        appendProductWrites(output, execution);
        appendCacheHits(output, execution);
        appendExecutionDiagnostics(output, execution);

        return ProductExecution{
            .exitCode = execution.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE,
            .text = output.str(),
        };
    }

} // namespace asharia::asset_processor
