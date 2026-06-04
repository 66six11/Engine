#include "asharia/asset_core/asset_metadata_io.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/archive/json_archive.hpp"

namespace asharia::asset {
    namespace {

        using archive::ArchiveMember;
        using archive::ArchiveValue;
        using archive::ArchiveValueKind;
        using namespace std::string_view_literals;

        struct HashTextField {
            std::string_view text;
            std::string_view fieldName;
            std::string_view context;
        };

        [[nodiscard]] Error assetMetadataIoError(std::string message) {
            return Error{ErrorDomain::Asset, 5, std::move(message)};
        }

        [[nodiscard]] std::string assetMetadataSourceLabel(const SourceAssetRecord& source) {
            return "guid=\"" + formatAssetGuid(source.guid) + "\" source=\"" + source.sourcePath +
                   "\"";
        }

        [[nodiscard]] bool containsName(std::span<const std::string_view> names,
                                        std::string_view name) noexcept {
            return std::ranges::any_of(
                names, [name](std::string_view allowed) { return allowed == name; });
        }

        [[nodiscard]] VoidResult
        validateObjectMembers(const ArchiveValue& value, std::string_view context,
                              std::span<const std::string_view> allowedMembers) {
            if (value.kind != ArchiveValueKind::Object) {
                return std::unexpected{
                    assetMetadataIoError(std::string{context} + " must be a JSON object.")};
            }

            for (const ArchiveMember& member : value.objectValue) {
                if (!containsName(allowedMembers, member.key)) {
                    return std::unexpected{assetMetadataIoError(
                        std::string{context} + " contains unknown member '" + member.key + "'.")};
                }
            }

            return {};
        }

        [[nodiscard]] Result<const ArchiveValue*> requiredMember(const ArchiveValue& object,
                                                                 std::string_view memberName,
                                                                 ArchiveValueKind expectedKind,
                                                                 std::string_view context) {
            const ArchiveValue* value = object.findMemberValue(memberName);
            if (value == nullptr) {
                return std::unexpected{assetMetadataIoError(std::string{context} +
                                                            " is missing required member '" +
                                                            std::string{memberName} + "'.")};
            }

            if (value->kind != expectedKind) {
                return std::unexpected{assetMetadataIoError(std::string{context} + " member '" +
                                                            std::string{memberName} +
                                                            "' has an unexpected type.")};
            }

            return value;
        }

        [[nodiscard]] Result<std::string> requiredString(const ArchiveValue& object,
                                                         std::string_view memberName,
                                                         std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::String, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            return (*value)->stringValue;
        }

        [[nodiscard]] Result<std::uint32_t> requiredUint32(const ArchiveValue& object,
                                                           std::string_view memberName,
                                                           std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::Integer, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }

            if ((*value)->integerValue <= 0 ||
                (*value)->integerValue >
                    static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                return std::unexpected{assetMetadataIoError(std::string{context} + " member '" +
                                                            std::string{memberName} +
                                                            "' must be a positive uint32 value.")};
            }

            return static_cast<std::uint32_t>((*value)->integerValue);
        }

        [[nodiscard]] std::string formatHash64(std::uint64_t value) {
            constexpr std::string_view kHexDigits = "0123456789abcdef";
            std::string text(16, '0');
            for (std::size_t index = 0; index < text.size(); ++index) {
                const auto shift = static_cast<std::uint32_t>((text.size() - index - 1) * 4);
                text[index] = kHexDigits[(value >> shift) & 0xFU];
            }
            return text;
        }

        [[nodiscard]] int lowercaseHexValue(char character) noexcept {
            if (character >= '0' && character <= '9') {
                return character - '0';
            }
            if (character >= 'a' && character <= 'f') {
                return 10 + character - 'a';
            }
            return -1;
        }

        [[nodiscard]] Result<std::uint64_t> parseHash64(const HashTextField& field) {
            if (field.text.size() != 16) {
                return std::unexpected{assetMetadataIoError(
                    std::string{field.context} + " member '" + std::string{field.fieldName} +
                    "' must be a 16-digit lowercase hex uint64 string.")};
            }

            std::uint64_t value{};
            for (const char character : field.text) {
                const int digit = lowercaseHexValue(character);
                if (digit < 0) {
                    return std::unexpected{assetMetadataIoError(
                        std::string{field.context} + " member '" + std::string{field.fieldName} +
                        "' must use lowercase hex.")};
                }
                value = (value << 4U) | static_cast<std::uint64_t>(digit);
            }

            if (value == 0) {
                return std::unexpected{
                    assetMetadataIoError(std::string{field.context} + " member '" +
                                         std::string{field.fieldName} + "' cannot be zero.")};
            }

            return value;
        }

        [[nodiscard]] Result<std::uint64_t> requiredHash64(const ArchiveValue& object,
                                                           std::string_view memberName,
                                                           std::string_view context) {
            auto text = requiredString(object, memberName, context);
            if (!text) {
                return std::unexpected{std::move(text.error())};
            }
            return parseHash64(HashTextField{
                .text = *text,
                .fieldName = memberName,
                .context = context,
            });
        }

        [[nodiscard]] ArchiveValue
        settingsArchiveValue(std::span<const AssetImportSetting> settings) {
            std::vector<ArchiveMember> members;
            members.reserve(settings.size());
            for (const AssetImportSetting& setting : settings) {
                members.push_back(ArchiveMember{
                    .key = setting.key,
                    .value = ArchiveValue::string(setting.value),
                });
            }
            return ArchiveValue::object(std::move(members));
        }

        [[nodiscard]] ArchiveValue documentArchiveValue(const AssetMetadataDocument& document) {
            const SourceAssetRecord& source = document.source;
            return ArchiveValue::object({
                ArchiveMember{
                    .key = "schema",
                    .value = ArchiveValue::string(std::string{kAssetMetadataSchema}),
                },
                ArchiveMember{
                    .key = "schemaVersion",
                    .value = ArchiveValue::integer(kAssetMetadataVersion),
                },
                ArchiveMember{
                    .key = "guid",
                    .value = ArchiveValue::string(formatAssetGuid(source.guid)),
                },
                ArchiveMember{
                    .key = "assetType",
                    .value = ArchiveValue::string(source.assetTypeName),
                },
                ArchiveMember{
                    .key = "sourcePath",
                    .value = ArchiveValue::string(source.sourcePath),
                },
                ArchiveMember{
                    .key = "sourceHash",
                    .value = ArchiveValue::string(formatHash64(source.sourceHash)),
                },
                ArchiveMember{
                    .key = "settingsHash",
                    .value = ArchiveValue::string(formatHash64(source.settingsHash)),
                },
                ArchiveMember{
                    .key = "importer",
                    .value = ArchiveValue::object({
                        ArchiveMember{
                            .key = "id",
                            .value = ArchiveValue::string(source.importerName),
                        },
                        ArchiveMember{
                            .key = "version",
                            .value = ArchiveValue::integer(source.importerVersion.value),
                        },
                    }),
                },
                ArchiveMember{
                    .key = "settings",
                    .value = settingsArchiveValue(document.settings),
                },
            });
        }

        [[nodiscard]] Result<std::vector<AssetImportSetting>>
        readSettings(const ArchiveValue& root) {
            auto settingsValue =
                requiredMember(root, "settings", ArchiveValueKind::Object, ".ameta root");
            if (!settingsValue) {
                return std::unexpected{std::move(settingsValue.error())};
            }

            std::vector<AssetImportSetting> settings;
            settings.reserve((*settingsValue)->objectValue.size());
            for (const ArchiveMember& member : (*settingsValue)->objectValue) {
                if (member.key.empty()) {
                    return std::unexpected{
                        assetMetadataIoError(".ameta settings cannot contain an empty key.")};
                }
                if (member.value.kind != ArchiveValueKind::String) {
                    return std::unexpected{
                        assetMetadataIoError(".ameta setting '" + member.key +
                                             "' must use a string value in metadata IO v1.")};
                }
                settings.push_back(AssetImportSetting{
                    .key = member.key,
                    .value = member.value.stringValue,
                });
            }

            return settings;
        }

        [[nodiscard]] Result<SourceAssetRecord> readSourceRecord(const ArchiveValue& root,
                                                                 std::uint64_t settingsHash) {
            auto guidText = requiredString(root, "guid", ".ameta root");
            if (!guidText) {
                return std::unexpected{std::move(guidText.error())};
            }
            auto guid = parseAssetGuid(*guidText);
            if (!guid) {
                return std::unexpected{std::move(guid.error())};
            }

            auto assetTypeName = requiredString(root, "assetType", ".ameta root");
            if (!assetTypeName) {
                return std::unexpected{std::move(assetTypeName.error())};
            }
            auto sourcePath = requiredString(root, "sourcePath", ".ameta root");
            if (!sourcePath) {
                return std::unexpected{std::move(sourcePath.error())};
            }
            auto sourceHash = requiredHash64(root, "sourceHash", ".ameta root");
            if (!sourceHash) {
                return std::unexpected{std::move(sourceHash.error())};
            }

            auto importerValue =
                requiredMember(root, "importer", ArchiveValueKind::Object, ".ameta root");
            if (!importerValue) {
                return std::unexpected{std::move(importerValue.error())};
            }
            constexpr std::array importerMembers{"id"sv, "version"sv};
            if (auto validImporter =
                    validateObjectMembers(**importerValue, ".ameta importer", importerMembers);
                !validImporter) {
                return std::unexpected{std::move(validImporter.error())};
            }

            auto importerName = requiredString(**importerValue, "id", ".ameta importer");
            if (!importerName) {
                return std::unexpected{std::move(importerName.error())};
            }
            auto importerVersion = requiredUint32(**importerValue, "version", ".ameta importer");
            if (!importerVersion) {
                return std::unexpected{std::move(importerVersion.error())};
            }

            return SourceAssetRecord{
                .guid = *guid,
                .assetType = makeAssetTypeId(*assetTypeName),
                .assetTypeName = std::move(*assetTypeName),
                .sourcePath = std::move(*sourcePath),
                .importerId = makeImporterId(*importerName),
                .importerName = std::move(*importerName),
                .importerVersion = ImporterVersion{*importerVersion},
                .sourceHash = *sourceHash,
                .settingsHash = settingsHash,
            };
        }

        [[nodiscard]] Result<AssetMetadataDocument>
        readAssetMetadataArchive(const ArchiveValue& archive) {
            constexpr std::array rootMembers{"schema"sv,       "schemaVersion"sv, "guid"sv,
                                             "assetType"sv,    "sourcePath"sv,    "sourceHash"sv,
                                             "settingsHash"sv, "importer"sv,      "settings"sv};
            if (auto validRoot = validateObjectMembers(archive, ".ameta root", rootMembers);
                !validRoot) {
                return std::unexpected{std::move(validRoot.error())};
            }

            auto schema = requiredString(archive, "schema", ".ameta root");
            if (!schema) {
                return std::unexpected{std::move(schema.error())};
            }
            if (*schema != kAssetMetadataSchema) {
                return std::unexpected{
                    assetMetadataIoError(".ameta root has unsupported schema '" + *schema + "'.")};
            }

            auto schemaVersion = requiredUint32(archive, "schemaVersion", ".ameta root");
            if (!schemaVersion) {
                return std::unexpected{std::move(schemaVersion.error())};
            }
            if (*schemaVersion != kAssetMetadataVersion) {
                return std::unexpected{
                    assetMetadataIoError(".ameta root has unsupported schemaVersion '" +
                                         std::to_string(*schemaVersion) + "'.")};
            }

            auto storedSettingsHash = requiredHash64(archive, "settingsHash", ".ameta root");
            if (!storedSettingsHash) {
                return std::unexpected{std::move(storedSettingsHash.error())};
            }

            auto settings = readSettings(archive);
            if (!settings) {
                return std::unexpected{std::move(settings.error())};
            }

            auto source = readSourceRecord(archive, *storedSettingsHash);
            if (!source) {
                return std::unexpected{std::move(source.error())};
            }

            AssetMetadataDocument document{
                .source = std::move(*source),
                .settings = std::move(*settings),
            };
            if (auto validDocument = validateAssetMetadataDocument(document); !validDocument) {
                return std::unexpected{std::move(validDocument.error())};
            }

            return document;
        }

    } // namespace

    VoidResult validateAssetMetadataDocument(const AssetMetadataDocument& document) {
        auto validSource = validateSourceAssetRecord(document.source);
        if (!validSource) {
            return std::unexpected{std::move(validSource.error())};
        }

        if (document.source.assetType != makeAssetTypeId(document.source.assetTypeName)) {
            return std::unexpected{assetMetadataIoError("Asset metadata document " +
                                                        assetMetadataSourceLabel(document.source) +
                                                        " has an asset type id/name mismatch.")};
        }

        if (document.source.importerId != makeImporterId(document.source.importerName)) {
            return std::unexpected{assetMetadataIoError("Asset metadata document " +
                                                        assetMetadataSourceLabel(document.source) +
                                                        " has an importer id/name mismatch.")};
        }

        if (document.source.sourceHash == 0) {
            return std::unexpected{assetMetadataIoError("Asset metadata document " +
                                                        assetMetadataSourceLabel(document.source) +
                                                        " is missing a source hash.")};
        }

        for (std::size_t index = 0; index < document.settings.size(); ++index) {
            const AssetImportSetting& setting = document.settings[index];
            if (setting.key.empty()) {
                return std::unexpected{assetMetadataIoError(
                    "Asset metadata document " + assetMetadataSourceLabel(document.source) +
                    " has an empty import setting key.")};
            }
            for (std::size_t otherIndex = index + 1; otherIndex < document.settings.size();
                 ++otherIndex) {
                if (setting.key == document.settings[otherIndex].key) {
                    return std::unexpected{assetMetadataIoError(
                        "Asset metadata document " + assetMetadataSourceLabel(document.source) +
                        " has duplicate import setting key '" + setting.key + "'.")};
                }
            }
        }

        const std::uint64_t computedSettingsHash = hashAssetImportSettings(document.settings);
        if (document.source.settingsHash != computedSettingsHash) {
            return std::unexpected{assetMetadataIoError(
                "Asset metadata document " + assetMetadataSourceLabel(document.source) +
                " has a settings hash mismatch expected=\"" +
                formatHash64(document.source.settingsHash) + "\" actual=\"" +
                formatHash64(computedSettingsHash) + "\".")};
        }

        return {};
    }

    Result<std::string> writeAssetMetadataText(const AssetMetadataDocument& document) {
        auto validDocument = validateAssetMetadataDocument(document);
        if (!validDocument) {
            return std::unexpected{std::move(validDocument.error())};
        }

        auto text = archive::writeJsonArchive(documentArchiveValue(document));
        if (!text) {
            return std::unexpected{assetMetadataIoError("Failed to write asset metadata for " +
                                                        assetMetadataSourceLabel(document.source) +
                                                        ": " + text.error().message)};
        }

        return *text;
    }

    VoidResult writeAssetMetadataFile(const std::filesystem::path& path,
                                      const AssetMetadataDocument& document) {
        auto validDocument = validateAssetMetadataDocument(document);
        if (!validDocument) {
            return std::unexpected{std::move(validDocument.error())};
        }

        auto written = archive::writeJsonArchiveFile(path, documentArchiveValue(document));
        if (!written) {
            return std::unexpected{assetMetadataIoError("Failed to write asset metadata file '" +
                                                        path.string() +
                                                        "': " + written.error().message)};
        }

        return {};
    }

    Result<AssetMetadataDocument> readAssetMetadataText(std::string_view text) {
        auto parsedArchive = archive::readJsonArchive(text);
        if (!parsedArchive) {
            return std::unexpected{assetMetadataIoError("Failed to read asset metadata: " +
                                                        parsedArchive.error().message)};
        }

        return readAssetMetadataArchive(*parsedArchive);
    }

    Result<AssetMetadataDocument> readAssetMetadataFile(const std::filesystem::path& path) {
        auto archive = archive::readJsonArchiveFile(path);
        if (!archive) {
            return std::unexpected{assetMetadataIoError("Failed to parse asset metadata file '" +
                                                        path.string() +
                                                        "': " + archive.error().message)};
        }

        auto document = readAssetMetadataArchive(*archive);
        if (!document) {
            return std::unexpected{assetMetadataIoError("Failed to validate asset metadata file '" +
                                                        path.string() +
                                                        "': " + document.error().message)};
        }

        return document;
    }

} // namespace asharia::asset
