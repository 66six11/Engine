#include "asharia/asset_core/asset_metadata.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <utility>

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

        [[nodiscard]] constexpr std::uint64_t hashUint64(std::uint64_t hash,
                                                         std::uint64_t value) noexcept {
            for (std::uint32_t shift = 0; shift < 64; shift += 8) {
                hash = hashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
            }
            return hash;
        }

        [[nodiscard]] constexpr std::uint64_t hashText(std::uint64_t hash,
                                                       std::string_view text) noexcept {
            hash = hashUint64(hash, text.size());
            for (const char character : text) {
                hash = hashByte(hash, static_cast<unsigned char>(character));
            }
            return hash;
        }

        [[nodiscard]] bool isAsciiAlpha(char character) noexcept {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        }

        [[nodiscard]] Error sourcePathError(std::string_view sourcePath, std::string_view reason) {
            return Error{
                ErrorDomain::Asset,
                3,
                "Invalid asset source path source=\"" + std::string{sourcePath} +
                    "\": " + std::string{reason},
            };
        }

        [[nodiscard]] Error sourceAssetRecordError(const SourceAssetRecord& record,
                                                   std::string_view reason) {
            return Error{
                ErrorDomain::Asset,
                3,
                "Invalid source asset record guid=\"" + formatAssetGuid(record.guid) +
                    "\" source=\"" + record.sourcePath + "\" assetType=\"" + record.assetTypeName +
                    "\" importer=\"" + record.importerName + "\": " + std::string{reason},
            };
        }

        [[nodiscard]] Error sourceAssetRecordSetError(const SourceAssetRecord& first,
                                                      const SourceAssetRecord& second,
                                                      std::string_view reason) {
            return Error{
                ErrorDomain::Asset,
                3,
                "Conflicting source asset records firstGuid=\"" + formatAssetGuid(first.guid) +
                    "\" firstSource=\"" + first.sourcePath + "\" secondGuid=\"" +
                    formatAssetGuid(second.guid) + "\" secondSource=\"" + second.sourcePath +
                    "\": " + std::string{reason},
            };
        }

    } // namespace

    ImporterId makeImporterId(std::string_view importerName) noexcept {
        if (importerName.empty()) {
            return {};
        }

        return ImporterId{hashText(kFnv1a64Offset, importerName)};
    }

    std::uint64_t hashAssetImportSettings(std::span<const AssetImportSetting> settings) noexcept {
        std::uint64_t hash = hashUint64(kFnv1a64Offset, settings.size());
        for (const AssetImportSetting& setting : settings) {
            hash = hashText(hash, setting.key);
            hash = hashText(hash, setting.value);
        }
        return hash;
    }

    VoidResult validateAssetSourcePath(std::string_view sourcePath) {
        if (sourcePath.empty()) {
            return std::unexpected{sourcePathError(sourcePath, "source path is missing")};
        }

        for (const char character : sourcePath) {
            const auto byte = static_cast<unsigned char>(character);
            if (byte < 0x20U || byte == 0x7FU) {
                return std::unexpected{
                    sourcePathError(sourcePath, "source path must not contain control characters")};
            }
        }

        if (sourcePath.find('\\') != std::string_view::npos) {
            return std::unexpected{
                sourcePathError(sourcePath, "source path must use '/' separators")};
        }

        if (sourcePath.front() == '/') {
            return std::unexpected{
                sourcePathError(sourcePath, "source path must be project-relative")};
        }

        if (sourcePath.size() >= 2 && isAsciiAlpha(sourcePath[0]) && sourcePath[1] == ':') {
            return std::unexpected{
                sourcePathError(sourcePath, "source path must not use a drive prefix")};
        }

        std::size_t segmentStart = 0;
        while (segmentStart <= sourcePath.size()) {
            const std::size_t segmentEnd = sourcePath.find('/', segmentStart);
            const std::size_t clampedEnd =
                segmentEnd == std::string_view::npos ? sourcePath.size() : segmentEnd;
            const std::string_view segment =
                sourcePath.substr(segmentStart, clampedEnd - segmentStart);

            if (segment.empty()) {
                return std::unexpected{
                    sourcePathError(sourcePath, "source path contains an empty segment")};
            }

            if (segment == "." || segment == "..") {
                return std::unexpected{sourcePathError(
                    sourcePath, "source path must not contain '.' or '..' segments")};
            }

            if (segmentEnd == std::string_view::npos) {
                break;
            }
            segmentStart = segmentEnd + 1;
        }

        return {};
    }

    VoidResult validateSourceAssetRecord(const SourceAssetRecord& record) {
        if (!record.guid) {
            return std::unexpected{sourceAssetRecordError(record, "asset GUID is invalid")};
        }

        if (!record.assetType) {
            return std::unexpected{sourceAssetRecordError(record, "asset type is missing")};
        }

        if (record.assetTypeName.empty()) {
            return std::unexpected{sourceAssetRecordError(record, "asset type name is missing")};
        }

        if (auto validSourcePath = validateAssetSourcePath(record.sourcePath); !validSourcePath) {
            return std::unexpected{sourceAssetRecordError(record, validSourcePath.error().message)};
        }

        if (!record.importerId) {
            return std::unexpected{sourceAssetRecordError(record, "importer id is missing")};
        }

        if (record.importerName.empty()) {
            return std::unexpected{sourceAssetRecordError(record, "importer name is missing")};
        }

        if (!record.importerVersion) {
            return std::unexpected{sourceAssetRecordError(record, "importer version is missing")};
        }

        return {};
    }

    VoidResult validateSourceAssetRecords(std::span<const SourceAssetRecord> records) {
        for (const SourceAssetRecord& record : records) {
            auto validRecord = validateSourceAssetRecord(record);
            if (!validRecord) {
                return std::unexpected{std::move(validRecord.error())};
            }
        }

        for (std::size_t firstIndex = 0; firstIndex < records.size(); ++firstIndex) {
            const SourceAssetRecord& first = records[firstIndex];
            for (std::size_t secondIndex = firstIndex + 1; secondIndex < records.size();
                 ++secondIndex) {
                const SourceAssetRecord& second = records[secondIndex];
                if (first.guid == second.guid && first.sourcePath != second.sourcePath) {
                    return std::unexpected{sourceAssetRecordSetError(
                        first, second, "duplicate asset GUID with different source path")};
                }

                if (first.sourcePath == second.sourcePath && first.guid != second.guid) {
                    return std::unexpected{sourceAssetRecordSetError(
                        first, second, "duplicate source path with different asset GUID")};
                }
            }
        }

        return {};
    }

} // namespace asharia::asset
