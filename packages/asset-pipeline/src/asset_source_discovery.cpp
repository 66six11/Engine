#include "asharia/asset_pipeline/asset_source_discovery.hpp"

#include <string>
#include <system_error>
#include <utility>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_metadata_io.hpp"

namespace asharia::asset {
    namespace {

        [[nodiscard]] std::string metadataPathText(const std::filesystem::path& path) {
            return path.generic_string();
        }

        [[nodiscard]] std::string entryLabel(const AssetSourceDiscoveryEntry& entry) {
            return "source=\"" + entry.sourcePath + "\" metadata=\"" +
                   metadataPathText(entry.metadataPath) + "\"";
        }

        void addDiagnostic(AssetSourceDiscoveryResult& result,
                           AssetSourceDiscoveryDiagnosticCode code,
                           const AssetSourceDiscoveryEntry& entry, std::string message) {
            result.diagnostics.push_back(AssetSourceDiscoveryDiagnostic{
                .code = code,
                .sourcePath = entry.sourcePath,
                .metadataPath = entry.metadataPath,
                .message = std::move(message),
            });
        }

        [[nodiscard]] bool metadataFileExists(const std::filesystem::path& path,
                                              std::error_code& error) {
            error.clear();
            const bool exists = std::filesystem::exists(path, error);
            if (error || !exists) {
                return false;
            }

            error.clear();
            const bool regularFile = std::filesystem::is_regular_file(path, error);
            return !error && regularFile;
        }

        [[nodiscard]] bool validateEntry(AssetSourceDiscoveryResult& result,
                                         const AssetSourceDiscoveryEntry& entry) {
            if (auto validSourcePath = validateAssetSourcePath(entry.sourcePath);
                !validSourcePath) {
                addDiagnostic(result, AssetSourceDiscoveryDiagnosticCode::InvalidEntry, entry,
                              "Asset source discovery entry has invalid source path: " +
                                  validSourcePath.error().message);
                return false;
            }

            if (entry.metadataPath.empty()) {
                addDiagnostic(result, AssetSourceDiscoveryDiagnosticCode::InvalidEntry, entry,
                              "Asset source discovery entry for source=\"" + entry.sourcePath +
                                  "\" is missing a metadata path.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool validateMetadataFile(AssetSourceDiscoveryResult& result,
                                                const AssetSourceDiscoveryEntry& entry) {
            std::error_code metadataError;
            if (metadataFileExists(entry.metadataPath, metadataError)) {
                return true;
            }

            std::string message = "Asset source discovery could not find metadata file for " +
                                  entryLabel(entry) + ".";
            if (metadataError) {
                message += " Filesystem error: " + metadataError.message() + ".";
            }
            addDiagnostic(result, AssetSourceDiscoveryDiagnosticCode::MissingMetadata, entry,
                          std::move(message));
            return false;
        }

        [[nodiscard]] bool validateSourcePathMatch(AssetSourceDiscoveryResult& result,
                                                   const AssetSourceDiscoveryEntry& entry,
                                                   const SourceAssetRecord& source) {
            if (source.sourcePath == entry.sourcePath) {
                return true;
            }

            addDiagnostic(result, AssetSourceDiscoveryDiagnosticCode::SourcePathMismatch, entry,
                          "Asset source discovery source path mismatch entry source=\"" +
                              entry.sourcePath + "\" metadata source=\"" + source.sourcePath +
                              "\" metadata=\"" + metadataPathText(entry.metadataPath) + "\".");
            return false;
        }

        [[nodiscard]] bool validateCatalogInsert(AssetSourceDiscoveryResult& result,
                                                 const AssetSourceDiscoveryEntry& entry,
                                                 const SourceAssetRecord& source) {
            if (const SourceAssetRecord* existing =
                    result.manifest.catalog.findByGuid(source.guid)) {
                addDiagnostic(result, AssetSourceDiscoveryDiagnosticCode::DuplicateGuid, entry,
                              "Asset source discovery duplicate GUID guid=\"" +
                                  formatAssetGuid(source.guid) + "\" existing source=\"" +
                                  existing->sourcePath + "\" new source=\"" + source.sourcePath +
                                  "\" metadata=\"" + metadataPathText(entry.metadataPath) + "\".");
                return false;
            }

            if (const SourceAssetRecord* existing =
                    result.manifest.catalog.findBySourcePath(source.sourcePath)) {
                addDiagnostic(
                    result, AssetSourceDiscoveryDiagnosticCode::DuplicateSourcePath, entry,
                    "Asset source discovery duplicate source path source=\"" + source.sourcePath +
                        "\" existing guid=\"" + formatAssetGuid(existing->guid) + "\" new guid=\"" +
                        formatAssetGuid(source.guid) + "\" metadata=\"" +
                        metadataPathText(entry.metadataPath) + "\".");
                return false;
            }

            auto added = result.manifest.catalog.addSource(source);
            if (!added) {
                addDiagnostic(result, AssetSourceDiscoveryDiagnosticCode::CatalogRejected, entry,
                              "Asset source discovery catalog rejected " + entryLabel(entry) +
                                  ": " + added.error().message);
                return false;
            }

            return true;
        }

    } // namespace

    AssetSourceDiscoveryResult
    discoverAssetSources(std::span<const AssetSourceDiscoveryEntry> entries) {
        AssetSourceDiscoveryResult result;
        result.manifest.records.reserve(entries.size());
        result.diagnostics.reserve(entries.size());

        for (const AssetSourceDiscoveryEntry& entry : entries) {
            if (!validateEntry(result, entry) || !validateMetadataFile(result, entry)) {
                continue;
            }

            auto document = readAssetMetadataFile(entry.metadataPath);
            if (!document) {
                addDiagnostic(result, AssetSourceDiscoveryDiagnosticCode::MetadataReadFailed, entry,
                              "Asset source discovery failed to read metadata for " +
                                  entryLabel(entry) + ": " + document.error().message);
                continue;
            }

            if (!validateSourcePathMatch(result, entry, document->source) ||
                !validateCatalogInsert(result, entry, document->source)) {
                continue;
            }

            result.manifest.records.push_back(DiscoveredSourceAsset{
                .entry = entry,
                .source = document->source,
                .settings = std::move(document->settings),
            });
        }

        return result;
    }

} // namespace asharia::asset
