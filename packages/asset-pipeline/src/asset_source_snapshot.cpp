#include "asharia/asset_pipeline/asset_source_snapshot.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>

#include "asharia/asset_core/asset_metadata.hpp"

namespace asharia::asset {
    namespace {

        constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

        [[nodiscard]] constexpr std::uint64_t hashByte(std::uint64_t hash,
                                                       std::uint8_t byte) noexcept {
            hash ^= byte;
            hash *= kFnv1a64Prime;
            return hash;
        }

        [[nodiscard]] std::string filePathText(const std::filesystem::path& path) {
            return path.generic_string();
        }

        [[nodiscard]] std::string entryLabel(const AssetSourceSnapshotEntry& entry) {
            return "source=\"" + entry.sourcePath + "\" file=\"" +
                   filePathText(entry.sourceFilePath) + "\"";
        }

        void addDiagnostic(AssetSourceSnapshotResult& result,
                           AssetSourceSnapshotDiagnosticCode code,
                           const AssetSourceSnapshotEntry& entry, std::string message) {
            result.diagnostics.push_back(AssetSourceSnapshotDiagnostic{
                .code = code,
                .sourcePath = entry.sourcePath,
                .sourceFilePath = entry.sourceFilePath,
                .message = std::move(message),
            });
        }

        [[nodiscard]] bool sourcePathAlreadySeen(std::span<const std::string> seenSourcePaths,
                                                 std::string_view sourcePath) {
            return std::ranges::any_of(seenSourcePaths, [sourcePath](const std::string& seen) {
                return seen == sourcePath;
            });
        }

        [[nodiscard]] bool validateEntry(AssetSourceSnapshotResult& result,
                                         const AssetSourceSnapshotEntry& entry) {
            if (auto validSourcePath = validateAssetSourcePath(entry.sourcePath);
                !validSourcePath) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::InvalidEntry, entry,
                              "Asset source snapshot entry has invalid source path: " +
                                  validSourcePath.error().message);
                return false;
            }

            if (entry.sourceFilePath.empty()) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::InvalidEntry, entry,
                              "Asset source snapshot entry for source=\"" + entry.sourcePath +
                                  "\" is missing a source file path.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool validateSourceFile(AssetSourceSnapshotResult& result,
                                              const AssetSourceSnapshotEntry& entry) {
            std::error_code fileError;
            const bool exists = std::filesystem::exists(entry.sourceFilePath, fileError);
            if (fileError) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::SourceFileReadFailed,
                              entry,
                              "Asset source snapshot could not query " + entryLabel(entry) + ": " +
                                  fileError.message() + ".");
                return false;
            }

            if (!exists) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::MissingSourceFile, entry,
                              "Asset source snapshot could not find source file for " +
                                  entryLabel(entry) + ".");
                return false;
            }

            fileError.clear();
            const bool regularFile =
                std::filesystem::is_regular_file(entry.sourceFilePath, fileError);
            if (fileError) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::SourceFileReadFailed,
                              entry,
                              "Asset source snapshot could not inspect " + entryLabel(entry) +
                                  ": " + fileError.message() + ".");
                return false;
            }

            if (!regularFile) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::SourceFileNotRegular,
                              entry,
                              "Asset source snapshot source file for " + entryLabel(entry) +
                                  " is not a regular file.");
                return false;
            }

            return true;
        }

        [[nodiscard]] bool hashSourceFile(AssetSourceSnapshotResult& result,
                                          const AssetSourceSnapshotEntry& entry,
                                          std::uint64_t& sourceHash) {
            std::ifstream file(entry.sourceFilePath, std::ios::binary);
            if (!file) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::SourceFileReadFailed,
                              entry,
                              "Asset source snapshot could not open " + entryLabel(entry) + ".");
                return false;
            }

            std::uint64_t hash = kFnv1a64Offset;
            std::array<char, 4096> buffer{};
            while (file) {
                file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize bytesRead = file.gcount();
                const auto end = buffer.begin() + bytesRead;
                for (auto byte = buffer.begin(); byte != end; ++byte) {
                    hash = hashByte(hash, static_cast<std::uint8_t>(*byte));
                }
            }

            if (file.bad()) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::SourceFileReadFailed,
                              entry,
                              "Asset source snapshot could not read " + entryLabel(entry) + ".");
                return false;
            }

            sourceHash = hash;
            return true;
        }

    } // namespace

    AssetSourceSnapshotResult
    snapshotAssetSourceFiles(std::span<const AssetSourceSnapshotEntry> entries) {
        AssetSourceSnapshotResult result;
        result.snapshots.reserve(entries.size());
        result.diagnostics.reserve(entries.size());

        std::vector<std::string> seenSourcePaths;
        seenSourcePaths.reserve(entries.size());

        for (const AssetSourceSnapshotEntry& entry : entries) {
            if (!validateEntry(result, entry)) {
                continue;
            }

            if (sourcePathAlreadySeen(seenSourcePaths, entry.sourcePath)) {
                addDiagnostic(result, AssetSourceSnapshotDiagnosticCode::DuplicateSourcePath, entry,
                              "Asset source snapshot duplicate source path source=\"" +
                                  entry.sourcePath + "\" file=\"" +
                                  filePathText(entry.sourceFilePath) + "\".");
                continue;
            }
            seenSourcePaths.push_back(entry.sourcePath);

            if (!validateSourceFile(result, entry)) {
                continue;
            }

            std::uint64_t sourceHash{};
            if (!hashSourceFile(result, entry, sourceHash)) {
                continue;
            }

            result.snapshots.push_back(AssetSourceSnapshot{
                .sourcePath = entry.sourcePath,
                .sourceFilePath = entry.sourceFilePath,
                .sourceHash = sourceHash,
            });
        }

        return result;
    }

} // namespace asharia::asset
