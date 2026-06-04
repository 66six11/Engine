#include "asharia/asset_pipeline/asset_product_manifest_io.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/archive/json_archive.hpp"
#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_metadata.hpp"
#include "asharia/asset_core/asset_type.hpp"

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

        [[nodiscard]] Error productManifestIoError(std::string message) {
            return Error{ErrorDomain::Asset, 6, std::move(message)};
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
                    productManifestIoError(std::string{context} + " must be a JSON object.")};
            }

            for (const ArchiveMember& member : value.objectValue) {
                if (!containsName(allowedMembers, member.key)) {
                    return std::unexpected{productManifestIoError(
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
                return std::unexpected{productManifestIoError(std::string{context} +
                                                              " is missing required member '" +
                                                              std::string{memberName} + "'.")};
            }

            if (value->kind != expectedKind) {
                return std::unexpected{productManifestIoError(std::string{context} + " member '" +
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
                return std::unexpected{productManifestIoError(
                    std::string{context} + " member '" + std::string{memberName} +
                    "' must be a positive uint32 value.")};
            }

            return static_cast<std::uint32_t>((*value)->integerValue);
        }

        [[nodiscard]] Result<std::uint64_t> requiredUint64Integer(const ArchiveValue& object,
                                                                  std::string_view memberName,
                                                                  std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::Integer, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }

            if ((*value)->integerValue < 0) {
                return std::unexpected{productManifestIoError(
                    std::string{context} + " member '" + std::string{memberName} +
                    "' must be a non-negative uint64 value.")};
            }

            return static_cast<std::uint64_t>((*value)->integerValue);
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
                return std::unexpected{productManifestIoError(
                    std::string{field.context} + " member '" + std::string{field.fieldName} +
                    "' must be a 16-digit lowercase hex uint64 string.")};
            }

            std::uint64_t value{};
            for (const char character : field.text) {
                const int digit = lowercaseHexValue(character);
                if (digit < 0) {
                    return std::unexpected{productManifestIoError(
                        std::string{field.context} + " member '" + std::string{field.fieldName} +
                        "' must use lowercase hex.")};
                }
                value = (value << 4U) | static_cast<std::uint64_t>(digit);
            }

            if (value == 0) {
                return std::unexpected{
                    productManifestIoError(std::string{field.context} + " member '" +
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

        [[nodiscard]] bool isAsciiAlpha(char character) noexcept {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        }

        [[nodiscard]] std::string productLabel(const AssetProductRecord& product) {
            return "guid=\"" + formatAssetGuid(product.key.guid) + "\" product=\"" +
                   product.relativeProductPath + "\"";
        }

        [[nodiscard]] ArchiveValue productArchiveValue(const AssetProductRecord& product) {
            return ArchiveValue::object({
                ArchiveMember{
                    .key = "guid",
                    .value = ArchiveValue::string(formatAssetGuid(product.key.guid)),
                },
                ArchiveMember{
                    .key = "assetType",
                    .value = ArchiveValue::string(formatHash64(product.key.assetType.value)),
                },
                ArchiveMember{
                    .key = "importerId",
                    .value = ArchiveValue::string(formatHash64(product.key.importerId.value)),
                },
                ArchiveMember{
                    .key = "importerVersion",
                    .value = ArchiveValue::integer(product.key.importerVersion.value),
                },
                ArchiveMember{
                    .key = "sourceHash",
                    .value = ArchiveValue::string(formatHash64(product.key.sourceHash)),
                },
                ArchiveMember{
                    .key = "settingsHash",
                    .value = ArchiveValue::string(formatHash64(product.key.settingsHash)),
                },
                ArchiveMember{
                    .key = "dependencyHash",
                    .value = ArchiveValue::string(formatHash64(product.key.dependencyHash)),
                },
                ArchiveMember{
                    .key = "targetProfileHash",
                    .value = ArchiveValue::string(formatHash64(product.key.targetProfileHash)),
                },
                ArchiveMember{
                    .key = "productKeyHash",
                    .value = ArchiveValue::string(formatHash64(hashAssetProductKey(product.key))),
                },
                ArchiveMember{
                    .key = "productPath",
                    .value = ArchiveValue::string(product.relativeProductPath),
                },
                ArchiveMember{
                    .key = "productSizeBytes",
                    .value =
                        ArchiveValue::integer(static_cast<std::int64_t>(product.productSizeBytes)),
                },
                ArchiveMember{
                    .key = "productHash",
                    .value = ArchiveValue::string(formatHash64(product.productHash)),
                },
            });
        }

        [[nodiscard]] ArchiveValue
        documentArchiveValue(const AssetProductManifestDocument& document) {
            std::vector<ArchiveValue> products;
            products.reserve(document.products.size());
            for (const AssetProductRecord& product : document.products) {
                products.push_back(productArchiveValue(product));
            }

            return ArchiveValue::object({
                ArchiveMember{
                    .key = "schema",
                    .value = ArchiveValue::string(std::string{kAssetProductManifestSchema}),
                },
                ArchiveMember{
                    .key = "schemaVersion",
                    .value = ArchiveValue::integer(kAssetProductManifestVersion),
                },
                ArchiveMember{
                    .key = "products",
                    .value = ArchiveValue::array(std::move(products)),
                },
            });
        }

        [[nodiscard]] Result<AssetProductRecord> readProductRecord(const ArchiveValue& product,
                                                                   std::size_t index) {
            const std::string context =
                "asset product manifest products[" + std::to_string(index) + "]";
            constexpr std::array productMembers{
                "guid"sv,           "assetType"sv,    "importerId"sv,       "importerVersion"sv,
                "sourceHash"sv,     "settingsHash"sv, "dependencyHash"sv,   "targetProfileHash"sv,
                "productKeyHash"sv, "productPath"sv,  "productSizeBytes"sv, "productHash"sv,
            };
            if (auto validProduct = validateObjectMembers(product, context, productMembers);
                !validProduct) {
                return std::unexpected{std::move(validProduct.error())};
            }

            auto guidText = requiredString(product, "guid", context);
            if (!guidText) {
                return std::unexpected{std::move(guidText.error())};
            }
            auto guid = parseAssetGuid(*guidText);
            if (!guid) {
                return std::unexpected{std::move(guid.error())};
            }

            auto assetType = requiredHash64(product, "assetType", context);
            if (!assetType) {
                return std::unexpected{std::move(assetType.error())};
            }
            auto importerId = requiredHash64(product, "importerId", context);
            if (!importerId) {
                return std::unexpected{std::move(importerId.error())};
            }
            auto importerVersion = requiredUint32(product, "importerVersion", context);
            if (!importerVersion) {
                return std::unexpected{std::move(importerVersion.error())};
            }
            auto sourceHash = requiredHash64(product, "sourceHash", context);
            if (!sourceHash) {
                return std::unexpected{std::move(sourceHash.error())};
            }
            auto settingsHash = requiredHash64(product, "settingsHash", context);
            if (!settingsHash) {
                return std::unexpected{std::move(settingsHash.error())};
            }
            auto dependencyHash = requiredHash64(product, "dependencyHash", context);
            if (!dependencyHash) {
                return std::unexpected{std::move(dependencyHash.error())};
            }
            auto targetProfileHash = requiredHash64(product, "targetProfileHash", context);
            if (!targetProfileHash) {
                return std::unexpected{std::move(targetProfileHash.error())};
            }
            auto productKeyHash = requiredHash64(product, "productKeyHash", context);
            if (!productKeyHash) {
                return std::unexpected{std::move(productKeyHash.error())};
            }
            auto productPath = requiredString(product, "productPath", context);
            if (!productPath) {
                return std::unexpected{std::move(productPath.error())};
            }
            auto productSizeBytes = requiredUint64Integer(product, "productSizeBytes", context);
            if (!productSizeBytes) {
                return std::unexpected{std::move(productSizeBytes.error())};
            }
            auto productHash = requiredHash64(product, "productHash", context);
            if (!productHash) {
                return std::unexpected{std::move(productHash.error())};
            }

            AssetProductRecord record{
                .key =
                    AssetProductKey{
                        .guid = *guid,
                        .assetType = AssetTypeId{*assetType},
                        .importerId = ImporterId{*importerId},
                        .importerVersion = ImporterVersion{*importerVersion},
                        .sourceHash = *sourceHash,
                        .settingsHash = *settingsHash,
                        .dependencyHash = *dependencyHash,
                        .targetProfileHash = *targetProfileHash,
                    },
                .relativeProductPath = std::move(*productPath),
                .productSizeBytes = *productSizeBytes,
                .productHash = *productHash,
            };

            const std::uint64_t computedKeyHash = hashAssetProductKey(record.key);
            if (computedKeyHash != *productKeyHash) {
                return std::unexpected{productManifestIoError(
                    context + " has a product key hash mismatch expected=\"" +
                    formatHash64(*productKeyHash) + "\" actual=\"" + formatHash64(computedKeyHash) +
                    "\".")};
            }

            return record;
        }

        [[nodiscard]] Result<AssetProductManifestDocument>
        readProductManifestArchive(const ArchiveValue& archive) {
            constexpr std::array rootMembers{"schema"sv, "schemaVersion"sv, "products"sv};
            if (auto validRoot =
                    validateObjectMembers(archive, "asset product manifest root", rootMembers);
                !validRoot) {
                return std::unexpected{std::move(validRoot.error())};
            }

            auto schema = requiredString(archive, "schema", "asset product manifest root");
            if (!schema) {
                return std::unexpected{std::move(schema.error())};
            }
            if (*schema != kAssetProductManifestSchema) {
                return std::unexpected{productManifestIoError(
                    "asset product manifest root has unsupported schema '" + *schema + "'.")};
            }

            auto schemaVersion =
                requiredUint32(archive, "schemaVersion", "asset product manifest root");
            if (!schemaVersion) {
                return std::unexpected{std::move(schemaVersion.error())};
            }
            if (*schemaVersion != kAssetProductManifestVersion) {
                return std::unexpected{productManifestIoError(
                    "asset product manifest root has unsupported schemaVersion '" +
                    std::to_string(*schemaVersion) + "'.")};
            }

            auto productsValue = requiredMember(archive, "products", ArchiveValueKind::Array,
                                                "asset product manifest root");
            if (!productsValue) {
                return std::unexpected{std::move(productsValue.error())};
            }

            AssetProductManifestDocument document;
            document.products.reserve((*productsValue)->arrayValue.size());
            for (std::size_t index = 0; index < (*productsValue)->arrayValue.size(); ++index) {
                auto product = readProductRecord((*productsValue)->arrayValue[index], index);
                if (!product) {
                    return std::unexpected{std::move(product.error())};
                }
                document.products.push_back(std::move(*product));
            }

            if (auto validDocument = validateAssetProductManifestDocument(document);
                !validDocument) {
                return std::unexpected{std::move(validDocument.error())};
            }

            return document;
        }

    } // namespace

    VoidResult validateAssetProductPath(std::string_view productPath) {
        auto pathError = [productPath](std::string_view reason) {
            return productManifestIoError("Invalid asset product path product=\"" +
                                          std::string{productPath} + "\": " + std::string{reason});
        };

        if (productPath.empty()) {
            return std::unexpected{pathError("product path is missing")};
        }

        if (productPath.find('\\') != std::string_view::npos) {
            return std::unexpected{pathError("product path must use '/' separators")};
        }

        if (productPath.front() == '/') {
            return std::unexpected{pathError("product path must be manifest-relative")};
        }

        if (productPath.size() >= 2 && isAsciiAlpha(productPath[0]) && productPath[1] == ':') {
            return std::unexpected{pathError("product path must not use a drive prefix")};
        }

        std::size_t segmentStart = 0;
        while (segmentStart <= productPath.size()) {
            const std::size_t segmentEnd = productPath.find('/', segmentStart);
            const std::size_t clampedEnd =
                segmentEnd == std::string_view::npos ? productPath.size() : segmentEnd;
            const std::string_view segment =
                productPath.substr(segmentStart, clampedEnd - segmentStart);

            if (segment.empty()) {
                return std::unexpected{pathError("product path contains an empty segment")};
            }

            if (segment == "." || segment == "..") {
                return std::unexpected{
                    pathError("product path must not contain '.' or '..' segments")};
            }

            if (segmentEnd == std::string_view::npos) {
                break;
            }
            segmentStart = segmentEnd + 1;
        }

        return {};
    }

    VoidResult validateAssetProductManifestDocument(const AssetProductManifestDocument& document) {
        for (std::size_t index = 0; index < document.products.size(); ++index) {
            const AssetProductRecord& product = document.products[index];
            const std::string context = "Asset product manifest product[" + std::to_string(index) +
                                        "] " + productLabel(product);

            if (!product.key) {
                return std::unexpected{
                    productManifestIoError(context + " has an incomplete product key.")};
            }

            if (product.key.dependencyHash == 0) {
                return std::unexpected{
                    productManifestIoError(context + " is missing a dependency hash.")};
            }

            if (auto validProductPath = validateAssetProductPath(product.relativeProductPath);
                !validProductPath) {
                return std::unexpected{
                    productManifestIoError(context + ": " + validProductPath.error().message)};
            }

            if (product.productSizeBytes >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                return std::unexpected{productManifestIoError(
                    context + " product size is too large for manifest IO v1.")};
            }

            if (product.productHash == 0) {
                return std::unexpected{
                    productManifestIoError(context + " is missing a product hash.")};
            }

            for (std::size_t otherIndex = index + 1; otherIndex < document.products.size();
                 ++otherIndex) {
                const AssetProductRecord& other = document.products[otherIndex];
                if (product.key == other.key) {
                    return std::unexpected{productManifestIoError(
                        context + " duplicates product key with product[" +
                        std::to_string(otherIndex) + "] " + productLabel(other) + ".")};
                }

                if (hashAssetProductKey(product.key) == hashAssetProductKey(other.key)) {
                    return std::unexpected{productManifestIoError(
                        context + " duplicates product key hash with product[" +
                        std::to_string(otherIndex) + "] " + productLabel(other) + ".")};
                }

                if (product.relativeProductPath == other.relativeProductPath) {
                    return std::unexpected{productManifestIoError(
                        context + " duplicates product path with product[" +
                        std::to_string(otherIndex) + "] " + productLabel(other) + ".")};
                }
            }
        }

        return {};
    }

    Result<std::string>
    writeAssetProductManifestText(const AssetProductManifestDocument& document) {
        auto validDocument = validateAssetProductManifestDocument(document);
        if (!validDocument) {
            return std::unexpected{std::move(validDocument.error())};
        }

        auto text = archive::writeJsonArchive(documentArchiveValue(document));
        if (!text) {
            return std::unexpected{productManifestIoError(
                "Failed to write asset product manifest: " + text.error().message)};
        }

        return *text;
    }

    VoidResult writeAssetProductManifestFile(const std::filesystem::path& path,
                                             const AssetProductManifestDocument& document) {
        auto validDocument = validateAssetProductManifestDocument(document);
        if (!validDocument) {
            return std::unexpected{std::move(validDocument.error())};
        }

        auto written = archive::writeJsonArchiveFile(path, documentArchiveValue(document));
        if (!written) {
            return std::unexpected{
                productManifestIoError("Failed to write asset product manifest file '" +
                                       path.string() + "': " + written.error().message)};
        }

        return {};
    }

    Result<AssetProductManifestDocument> readAssetProductManifestText(std::string_view text) {
        auto parsedArchive = archive::readJsonArchive(text);
        if (!parsedArchive) {
            return std::unexpected{productManifestIoError(
                "Failed to read asset product manifest: " + parsedArchive.error().message)};
        }

        return readProductManifestArchive(*parsedArchive);
    }

    Result<AssetProductManifestDocument>
    readAssetProductManifestFile(const std::filesystem::path& path) {
        auto archive = archive::readJsonArchiveFile(path);
        if (!archive) {
            return std::unexpected{
                productManifestIoError("Failed to parse asset product manifest file '" +
                                       path.string() + "': " + archive.error().message)};
        }

        auto document = readProductManifestArchive(*archive);
        if (!document) {
            return std::unexpected{
                productManifestIoError("Failed to validate asset product manifest file '" +
                                       path.string() + "': " + document.error().message)};
        }

        return document;
    }

} // namespace asharia::asset
