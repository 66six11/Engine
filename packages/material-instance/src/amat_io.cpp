#include "asharia/material_instance/amat_io.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/archive/json_archive.hpp"

namespace asharia::material_instance {
    namespace {

        using archive::ArchiveMember;
        using archive::ArchiveValue;
        using archive::ArchiveValueKind;
        using shader_authoring::AshaderPropertyType;
        using namespace std::string_view_literals;

        struct HashTextField {
            std::string_view text;
            std::string_view fieldName;
            std::string_view context;
        };

        [[nodiscard]] Error amatIoError(std::string message) {
            return Error{ErrorDomain::Material, 2, std::move(message)};
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
                    amatIoError(std::string{context} + " must be a JSON object.")};
            }

            for (const ArchiveMember& member : value.objectValue) {
                if (!containsName(allowedMembers, member.key)) {
                    return std::unexpected{amatIoError(
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
                return std::unexpected{amatIoError(std::string{context} +
                                                   " is missing required member '" +
                                                   std::string{memberName} + "'.")};
            }

            if (value->kind != expectedKind) {
                return std::unexpected{amatIoError(std::string{context} + " member '" +
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
                return std::unexpected{amatIoError(std::string{context} + " member '" +
                                                   std::string{memberName} +
                                                   "' must be a positive uint32 value.")};
            }

            return static_cast<std::uint32_t>((*value)->integerValue);
        }

        [[nodiscard]] Result<double> requiredNumberValue(const ArchiveValue& object,
                                                         std::string_view memberName,
                                                         std::string_view context) {
            auto value = requiredMember(object, memberName, ArchiveValueKind::Float, context);
            if (value) {
                return (*value)->floatValue;
            }

            value = requiredMember(object, memberName, ArchiveValueKind::Integer, context);
            if (value) {
                return static_cast<double>((*value)->integerValue);
            }

            return std::unexpected{amatIoError(std::string{context} + " member '" +
                                               std::string{memberName} + "' must be a number.")};
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
                return std::unexpected{amatIoError(std::string{field.context} + " member '" +
                                                   std::string{field.fieldName} +
                                                   "' must be a 16-digit lowercase hex string.")};
            }

            std::uint64_t value{};
            for (const char character : field.text) {
                const int digit = lowercaseHexValue(character);
                if (digit < 0) {
                    return std::unexpected{amatIoError(std::string{field.context} + " member '" +
                                                       std::string{field.fieldName} +
                                                       "' must use lowercase hex.")};
                }
                value = (value << 4U) | static_cast<std::uint64_t>(digit);
            }

            if (value == 0) {
                return std::unexpected{amatIoError(std::string{field.context} + " member '" +
                                                   std::string{field.fieldName} +
                                                   "' cannot be zero.")};
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

        [[nodiscard]] Result<AshaderPropertyType> parsePropertyType(std::string_view text,
                                                                    std::string_view context) {
            if (text == "float") {
                return AshaderPropertyType::Float;
            }
            if (text == "float2") {
                return AshaderPropertyType::Float2;
            }
            if (text == "float3") {
                return AshaderPropertyType::Float3;
            }
            if (text == "float4") {
                return AshaderPropertyType::Float4;
            }
            if (text == "color") {
                return AshaderPropertyType::Color;
            }
            if (text == "int") {
                return AshaderPropertyType::Int;
            }
            if (text == "uint") {
                return AshaderPropertyType::UInt;
            }
            if (text == "bool") {
                return AshaderPropertyType::Bool;
            }
            if (text == "texture2D") {
                return AshaderPropertyType::Texture2D;
            }
            if (text == "sampler") {
                return AshaderPropertyType::Sampler;
            }

            return std::unexpected{amatIoError(std::string{context} + " has unknown type '" +
                                               std::string{text} + "'.")};
        }

        [[nodiscard]] std::size_t vectorWidth(AshaderPropertyType type) noexcept {
            switch (type) {
            case AshaderPropertyType::Float2:
                return 2;
            case AshaderPropertyType::Float3:
                return 3;
            case AshaderPropertyType::Float4:
            case AshaderPropertyType::Color:
                return 4;
            default:
                return 0;
            }
        }

        [[nodiscard]] bool isAssetValue(AshaderPropertyType type) noexcept {
            return type == AshaderPropertyType::Texture2D || type == AshaderPropertyType::Sampler;
        }

        [[nodiscard]] Result<asset::AssetGuid> requiredAssetGuid(const ArchiveValue& object,
                                                                 std::string_view memberName,
                                                                 std::string_view context) {
            auto text = requiredString(object, memberName, context);
            if (!text) {
                return std::unexpected{std::move(text.error())};
            }
            auto guid = asset::parseAssetGuid(*text);
            if (!guid) {
                return std::unexpected{amatIoError(std::string{context} + " member '" +
                                                   std::string{memberName} +
                                                   "' is invalid: " + guid.error().message)};
            }
            return *guid;
        }

        [[nodiscard]] AmatPropertyValue makeNumberValue(double value) {
            AmatPropertyValue propertyValue{};
            propertyValue.kind = AmatPropertyValueKind::Number;
            propertyValue.numberValue = value;
            return propertyValue;
        }

        [[nodiscard]] AmatPropertyValue makeVectorValue(std::vector<double> values) {
            AmatPropertyValue propertyValue{};
            propertyValue.kind = AmatPropertyValueKind::Vector;
            propertyValue.vectorValue = std::move(values);
            return propertyValue;
        }

        [[nodiscard]] AmatPropertyValue makeIntegerValue(std::int64_t value) {
            AmatPropertyValue propertyValue{};
            propertyValue.kind = AmatPropertyValueKind::Integer;
            propertyValue.integerValue = value;
            return propertyValue;
        }

        [[nodiscard]] AmatPropertyValue makeUnsignedIntegerValue(std::uint64_t value) {
            AmatPropertyValue propertyValue{};
            propertyValue.kind = AmatPropertyValueKind::UnsignedInteger;
            propertyValue.unsignedIntegerValue = value;
            return propertyValue;
        }

        [[nodiscard]] AmatPropertyValue makeBooleanValue(bool value) {
            AmatPropertyValue propertyValue{};
            propertyValue.kind = AmatPropertyValueKind::Boolean;
            propertyValue.boolValue = value;
            return propertyValue;
        }

        [[nodiscard]] AmatPropertyValue makeAssetGuidValue(asset::AssetGuid value) {
            AmatPropertyValue propertyValue{};
            propertyValue.kind = AmatPropertyValueKind::AssetGuid;
            propertyValue.assetGuid = value;
            return propertyValue;
        }

        [[nodiscard]] Result<AmatPropertyValue> readNumberPropertyValue(const ArchiveValue& object,
                                                                        std::string_view context) {
            auto value = requiredNumberValue(object, "value", context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            return makeNumberValue(*value);
        }

        [[nodiscard]] Result<AmatPropertyValue> readVectorPropertyValue(const ArchiveValue& object,
                                                                        std::size_t width,
                                                                        std::string_view context) {
            auto value = requiredMember(object, "value", ArchiveValueKind::Array, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if ((*value)->arrayValue.size() != width) {
                return std::unexpected{amatIoError(std::string{context} +
                                                   " member 'value' must have " +
                                                   std::to_string(width) + " elements.")};
            }

            std::vector<double> elements;
            elements.reserve(width);
            for (std::size_t index = 0; index < (*value)->arrayValue.size(); ++index) {
                const ArchiveValue& element = (*value)->arrayValue[index];
                if (element.kind == ArchiveValueKind::Float) {
                    elements.push_back(element.floatValue);
                    continue;
                }
                if (element.kind == ArchiveValueKind::Integer) {
                    elements.push_back(static_cast<double>(element.integerValue));
                    continue;
                }
                return std::unexpected{amatIoError(std::string{context} +
                                                   " member 'value' element[" +
                                                   std::to_string(index) + "] must be a number.")};
            }
            return makeVectorValue(std::move(elements));
        }

        [[nodiscard]] Result<AmatPropertyValue> readIntegerPropertyValue(const ArchiveValue& object,
                                                                         std::string_view context) {
            auto value = requiredMember(object, "value", ArchiveValueKind::Integer, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            return makeIntegerValue((*value)->integerValue);
        }

        [[nodiscard]] Result<AmatPropertyValue>
        readUnsignedIntegerPropertyValue(const ArchiveValue& object, std::string_view context) {
            auto value = requiredMember(object, "value", ArchiveValueKind::Integer, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            if ((*value)->integerValue < 0) {
                return std::unexpected{
                    amatIoError(std::string{context} + " member 'value' must be non-negative.")};
            }
            return makeUnsignedIntegerValue(static_cast<std::uint64_t>((*value)->integerValue));
        }

        [[nodiscard]] Result<AmatPropertyValue> readBooleanPropertyValue(const ArchiveValue& object,
                                                                         std::string_view context) {
            auto value = requiredMember(object, "value", ArchiveValueKind::Bool, context);
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            return makeBooleanValue((*value)->boolValue);
        }

        [[nodiscard]] Result<AmatPropertyValue> readAssetPropertyValue(const ArchiveValue& object,
                                                                       std::string_view context) {
            auto guid = requiredAssetGuid(object, "assetGuid", context);
            if (!guid) {
                return std::unexpected{std::move(guid.error())};
            }
            return makeAssetGuidValue(*guid);
        }

        [[nodiscard]] Result<AmatPropertyValue> readPropertyValue(const ArchiveValue& object,
                                                                  AshaderPropertyType type,
                                                                  std::string_view context) {
            switch (type) {
            case AshaderPropertyType::Float:
                return readNumberPropertyValue(object, context);
            case AshaderPropertyType::Float2:
            case AshaderPropertyType::Float3:
            case AshaderPropertyType::Float4:
            case AshaderPropertyType::Color:
                return readVectorPropertyValue(object, vectorWidth(type), context);
            case AshaderPropertyType::Int:
                return readIntegerPropertyValue(object, context);
            case AshaderPropertyType::UInt:
                return readUnsignedIntegerPropertyValue(object, context);
            case AshaderPropertyType::Bool:
                return readBooleanPropertyValue(object, context);
            case AshaderPropertyType::Texture2D:
            case AshaderPropertyType::Sampler:
                return readAssetPropertyValue(object, context);
            }
            return std::unexpected{
                amatIoError(std::string{context} + " has unsupported value type.")};
        }

        [[nodiscard]] Result<AmatMaterialTypeReference>
        readMaterialTypeReference(const ArchiveValue& root) {
            auto value =
                requiredMember(root, "materialType", ArchiveValueKind::Object, ".amat root");
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            constexpr std::array materialTypeMembers{"assetGuid"sv, "stableTypeId"sv,
                                                     "expectedTypeHash"sv};
            if (auto valid =
                    validateObjectMembers(**value, ".amat materialType", materialTypeMembers);
                !valid) {
                return std::unexpected{std::move(valid.error())};
            }

            auto assetGuid = requiredAssetGuid(**value, "assetGuid", ".amat materialType");
            if (!assetGuid) {
                return std::unexpected{std::move(assetGuid.error())};
            }
            auto stableTypeId = requiredString(**value, "stableTypeId", ".amat materialType");
            if (!stableTypeId) {
                return std::unexpected{std::move(stableTypeId.error())};
            }
            if (stableTypeId->empty()) {
                return std::unexpected{
                    amatIoError(".amat materialType member 'stableTypeId' cannot be empty.")};
            }
            auto expectedTypeHash =
                requiredHash64(**value, "expectedTypeHash", ".amat materialType");
            if (!expectedTypeHash) {
                return std::unexpected{std::move(expectedTypeHash.error())};
            }

            return AmatMaterialTypeReference{
                .assetGuid = *assetGuid,
                .stableTypeId = std::move(*stableTypeId),
                .expectedTypeHash = *expectedTypeHash,
            };
        }

        [[nodiscard]] Result<std::vector<AmatPropertyOverride>>
        readPropertyOverrides(const ArchiveValue& root) {
            auto value = requiredMember(root, "properties", ArchiveValueKind::Object, ".amat root");
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }

            std::vector<AmatPropertyOverride> overrides;
            overrides.reserve((*value)->objectValue.size());
            for (const ArchiveMember& member : (*value)->objectValue) {
                const std::string context = ".amat properties." + member.key;
                constexpr std::array propertyMembers{"propertyId"sv, "type"sv, "value"sv,
                                                     "assetGuid"sv};
                if (auto valid = validateObjectMembers(member.value, context, propertyMembers);
                    !valid) {
                    return std::unexpected{std::move(valid.error())};
                }

                auto propertyId = requiredString(member.value, "propertyId", context);
                if (!propertyId) {
                    return std::unexpected{std::move(propertyId.error())};
                }
                if (*propertyId != member.key) {
                    return std::unexpected{amatIoError(context +
                                                       " member 'propertyId' must match " +
                                                       "the properties object key.")};
                }

                auto typeName = requiredString(member.value, "type", context);
                if (!typeName) {
                    return std::unexpected{std::move(typeName.error())};
                }
                auto type = parsePropertyType(*typeName, context);
                if (!type) {
                    return std::unexpected{std::move(type.error())};
                }

                const bool hasValue = member.value.findMemberValue("value") != nullptr;
                const bool hasAssetGuid = member.value.findMemberValue("assetGuid") != nullptr;
                if (isAssetValue(*type) && hasValue) {
                    return std::unexpected{
                        amatIoError(context + " must use 'assetGuid', not 'value'.")};
                }
                if (!isAssetValue(*type) && hasAssetGuid) {
                    return std::unexpected{
                        amatIoError(context + " must use 'value', not 'assetGuid'.")};
                }

                auto propertyValue = readPropertyValue(member.value, *type, context);
                if (!propertyValue) {
                    return std::unexpected{std::move(propertyValue.error())};
                }

                overrides.push_back(AmatPropertyOverride{
                    .propertyId = std::move(*propertyId),
                    .type = *type,
                    .value = std::move(*propertyValue),
                });
            }
            return overrides;
        }

        [[nodiscard]] Result<AmatImportMetadata> readImportMetadata(const ArchiveValue& root) {
            auto value = requiredMember(root, "import", ArchiveValueKind::Object, ".amat root");
            if (!value) {
                return std::unexpected{std::move(value.error())};
            }
            constexpr std::array importMembers{"lastCookedSignatureHash"sv, "lastCookedAt"sv};
            if (auto valid = validateObjectMembers(**value, ".amat import", importMembers);
                !valid) {
                return std::unexpected{std::move(valid.error())};
            }

            auto lastCookedSignatureHash =
                requiredHash64(**value, "lastCookedSignatureHash", ".amat import");
            if (!lastCookedSignatureHash) {
                return std::unexpected{std::move(lastCookedSignatureHash.error())};
            }

            std::string lastCookedAt;
            if (const ArchiveValue* lastCookedAtValue = (*value)->findMemberValue("lastCookedAt");
                lastCookedAtValue != nullptr) {
                if (lastCookedAtValue->kind != ArchiveValueKind::String) {
                    return std::unexpected{
                        amatIoError(".amat import member 'lastCookedAt' has an unexpected type.")};
                }
                lastCookedAt = lastCookedAtValue->stringValue;
            }

            return AmatImportMetadata{
                .lastCookedSignatureHash = *lastCookedSignatureHash,
                .lastCookedAt = std::move(lastCookedAt),
            };
        }

        [[nodiscard]] Result<AmatDocument> readAmatArchive(const ArchiveValue& archive) {
            constexpr std::array rootMembers{"schemaVersion"sv, "materialType"sv, "variant"sv,
                                             "properties"sv, "import"sv};
            if (auto validRoot = validateObjectMembers(archive, ".amat root", rootMembers);
                !validRoot) {
                return std::unexpected{std::move(validRoot.error())};
            }

            auto schemaVersion = requiredUint32(archive, "schemaVersion", ".amat root");
            if (!schemaVersion) {
                return std::unexpected{std::move(schemaVersion.error())};
            }
            if (*schemaVersion != kAmatSchemaVersion) {
                return std::unexpected{amatIoError(".amat root has unsupported schemaVersion '" +
                                                   std::to_string(*schemaVersion) + "'.")};
            }

            if (const ArchiveValue* variant = archive.findMemberValue("variant");
                variant != nullptr && variant->kind != ArchiveValueKind::Object) {
                return std::unexpected{amatIoError(".amat variant must be a JSON object.")};
            }

            auto materialType = readMaterialTypeReference(archive);
            if (!materialType) {
                return std::unexpected{std::move(materialType.error())};
            }
            auto properties = readPropertyOverrides(archive);
            if (!properties) {
                return std::unexpected{std::move(properties.error())};
            }
            auto import = readImportMetadata(archive);
            if (!import) {
                return std::unexpected{std::move(import.error())};
            }

            AmatDocument document{
                .schemaVersion = *schemaVersion,
                .materialType = std::move(*materialType),
                .properties = std::move(*properties),
                .import = std::move(*import),
            };
            if (auto validDocument = validateAmatDocument(document); !validDocument) {
                return std::unexpected{std::move(validDocument.error())};
            }
            return document;
        }

        [[nodiscard]] ArchiveValue valueArchiveValue(const AmatPropertyOverride& property) {
            switch (property.value.kind) {
            case AmatPropertyValueKind::Number:
                return ArchiveValue::floating(property.value.numberValue);
            case AmatPropertyValueKind::Integer:
                return ArchiveValue::integer(property.value.integerValue);
            case AmatPropertyValueKind::UnsignedInteger:
                return ArchiveValue::integer(
                    static_cast<std::int64_t>(property.value.unsignedIntegerValue));
            case AmatPropertyValueKind::Boolean:
                return ArchiveValue::boolean(property.value.boolValue);
            case AmatPropertyValueKind::Vector: {
                std::vector<ArchiveValue> values;
                values.reserve(property.value.vectorValue.size());
                for (const double element : property.value.vectorValue) {
                    values.push_back(ArchiveValue::floating(element));
                }
                return ArchiveValue::array(std::move(values));
            }
            case AmatPropertyValueKind::AssetGuid:
                return ArchiveValue::string(asset::formatAssetGuid(property.value.assetGuid));
            }
            return ArchiveValue::null();
        }

        [[nodiscard]] ArchiveValue propertyArchiveValue(const AmatPropertyOverride& property) {
            std::vector<ArchiveMember> members{
                ArchiveMember{
                    .key = "propertyId",
                    .value = ArchiveValue::string(property.propertyId),
                },
                ArchiveMember{
                    .key = "type",
                    .value = ArchiveValue::string(
                        std::string{shader_authoring::toString(property.type)}),
                },
            };

            if (isAssetValue(property.type)) {
                members.push_back(ArchiveMember{
                    .key = "assetGuid",
                    .value = valueArchiveValue(property),
                });
            } else {
                members.push_back(ArchiveMember{
                    .key = "value",
                    .value = valueArchiveValue(property),
                });
            }

            return ArchiveValue::object(std::move(members));
        }

        [[nodiscard]] ArchiveValue documentArchiveValue(const AmatDocument& document) {
            std::vector<AmatPropertyOverride> sortedProperties = document.properties;
            std::ranges::sort(sortedProperties, {}, &AmatPropertyOverride::propertyId);

            std::vector<ArchiveMember> properties;
            properties.reserve(sortedProperties.size());
            for (const AmatPropertyOverride& property : sortedProperties) {
                properties.push_back(ArchiveMember{
                    .key = property.propertyId,
                    .value = propertyArchiveValue(property),
                });
            }

            std::vector<ArchiveMember> importMembers{
                ArchiveMember{
                    .key = "lastCookedSignatureHash",
                    .value =
                        ArchiveValue::string(formatHash64(document.import.lastCookedSignatureHash)),
                },
            };
            if (!document.import.lastCookedAt.empty()) {
                importMembers.push_back(ArchiveMember{
                    .key = "lastCookedAt",
                    .value = ArchiveValue::string(document.import.lastCookedAt),
                });
            }

            return ArchiveValue::object({
                ArchiveMember{
                    .key = "schemaVersion",
                    .value = ArchiveValue::integer(kAmatSchemaVersion),
                },
                ArchiveMember{
                    .key = "materialType",
                    .value = ArchiveValue::object({
                        ArchiveMember{
                            .key = "assetGuid",
                            .value = ArchiveValue::string(
                                asset::formatAssetGuid(document.materialType.assetGuid)),
                        },
                        ArchiveMember{
                            .key = "stableTypeId",
                            .value = ArchiveValue::string(document.materialType.stableTypeId),
                        },
                        ArchiveMember{
                            .key = "expectedTypeHash",
                            .value = ArchiveValue::string(
                                formatHash64(document.materialType.expectedTypeHash)),
                        },
                    }),
                },
                ArchiveMember{
                    .key = "variant",
                    .value = ArchiveValue::object({
                        ArchiveMember{
                            .key = "staticSwitches",
                            .value = ArchiveValue::object({}),
                        },
                    }),
                },
                ArchiveMember{
                    .key = "properties",
                    .value = ArchiveValue::object(std::move(properties)),
                },
                ArchiveMember{
                    .key = "import",
                    .value = ArchiveValue::object(std::move(importMembers)),
                },
            });
        }

        [[nodiscard]] VoidResult validatePropertyIds(const AmatDocument& document) {
            for (std::size_t index = 0; index < document.properties.size(); ++index) {
                const AmatPropertyOverride& property = document.properties[index];
                if (property.propertyId.empty()) {
                    return std::unexpected{amatIoError("Amat property[" + std::to_string(index) +
                                                       "] has an empty propertyId.")};
                }
                for (std::size_t otherIndex = index + 1; otherIndex < document.properties.size();
                     ++otherIndex) {
                    if (property.propertyId == document.properties[otherIndex].propertyId) {
                        return std::unexpected{amatIoError("Amat property '" + property.propertyId +
                                                           "' is duplicated.")};
                    }
                }
            }
            return {};
        }

        [[nodiscard]] VoidResult validateAssetPropertyValue(const AmatPropertyOverride& property) {
            if (property.value.kind == AmatPropertyValueKind::AssetGuid &&
                static_cast<bool>(property.value.assetGuid)) {
                return {};
            }
            return std::unexpected{amatIoError("Amat property '" + property.propertyId +
                                               "' must carry an asset GUID value.")};
        }

        [[nodiscard]] VoidResult
        validateNumericPropertyValue(const AmatPropertyOverride& property) {
            const std::size_t width = vectorWidth(property.type);
            if (property.type == AshaderPropertyType::Float) {
                if (property.value.kind == AmatPropertyValueKind::Number) {
                    return {};
                }
                return std::unexpected{amatIoError("Amat property '" + property.propertyId +
                                                   "' must carry a number value.")};
            }
            if (width != 0) {
                if (property.value.kind == AmatPropertyValueKind::Vector &&
                    property.value.vectorValue.size() == width) {
                    return {};
                }
                return std::unexpected{amatIoError("Amat property '" + property.propertyId +
                                                   "' has an invalid vector value.")};
            }
            if (property.type == AshaderPropertyType::Int) {
                if (property.value.kind == AmatPropertyValueKind::Integer) {
                    return {};
                }
                return std::unexpected{amatIoError("Amat property '" + property.propertyId +
                                                   "' must carry an integer value.")};
            }
            if (property.type == AshaderPropertyType::UInt) {
                if (property.value.kind == AmatPropertyValueKind::UnsignedInteger &&
                    property.value.unsignedIntegerValue <=
                        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
                    return {};
                }
                return std::unexpected{amatIoError("Amat property '" + property.propertyId +
                                                   "' must carry a uint32 value.")};
            }
            if (property.type == AshaderPropertyType::Bool) {
                if (property.value.kind == AmatPropertyValueKind::Boolean) {
                    return {};
                }
                return std::unexpected{amatIoError("Amat property '" + property.propertyId +
                                                   "' must carry a bool value.")};
            }
            return std::unexpected{amatIoError("Amat property '" + property.propertyId +
                                               "' has unsupported value type.")};
        }

        [[nodiscard]] VoidResult validatePropertyValue(const AmatPropertyOverride& property) {
            if (isAssetValue(property.type)) {
                return validateAssetPropertyValue(property);
            }
            return validateNumericPropertyValue(property);
        }

    } // namespace

    std::string_view toString(AmatPropertyValueKind kind) noexcept {
        switch (kind) {
        case AmatPropertyValueKind::Number:
            return "number";
        case AmatPropertyValueKind::Integer:
            return "integer";
        case AmatPropertyValueKind::UnsignedInteger:
            return "uint";
        case AmatPropertyValueKind::Boolean:
            return "bool";
        case AmatPropertyValueKind::Vector:
            return "vector";
        case AmatPropertyValueKind::AssetGuid:
            return "assetGuid";
        }
        return "unknown";
    }

    VoidResult validateAmatDocument(const AmatDocument& document) {
        if (document.schemaVersion != kAmatSchemaVersion) {
            return std::unexpected{amatIoError("Amat document has unsupported schemaVersion '" +
                                               std::to_string(document.schemaVersion) + "'.")};
        }
        if (!document.materialType) {
            return std::unexpected{amatIoError("Amat document has an incomplete materialType.")};
        }
        if (document.import.lastCookedSignatureHash == 0) {
            return std::unexpected{
                amatIoError("Amat document is missing import.lastCookedSignatureHash.")};
        }
        if (auto validPropertyIds = validatePropertyIds(document); !validPropertyIds) {
            return std::unexpected{std::move(validPropertyIds.error())};
        }
        for (const AmatPropertyOverride& property : document.properties) {
            if (auto validValue = validatePropertyValue(property); !validValue) {
                return std::unexpected{std::move(validValue.error())};
            }
        }

        return {};
    }

    Result<AmatDocument> readAmatText(std::string_view text) {
        auto parsedArchive = archive::readJsonArchive(text);
        if (!parsedArchive) {
            return std::unexpected{
                amatIoError("Failed to read .amat JSON: " + parsedArchive.error().message)};
        }

        return readAmatArchive(*parsedArchive);
    }

    Result<AmatDocument> readAmatFile(const std::filesystem::path& path) {
        auto archive = archive::readJsonArchiveFile(path);
        if (!archive) {
            return std::unexpected{amatIoError("Failed to parse .amat file '" + path.string() +
                                               "': " + archive.error().message)};
        }

        auto document = readAmatArchive(*archive);
        if (!document) {
            return std::unexpected{amatIoError("Failed to validate .amat file '" + path.string() +
                                               "': " + document.error().message)};
        }
        return document;
    }

    Result<std::string> writeAmatText(const AmatDocument& document) {
        if (auto validDocument = validateAmatDocument(document); !validDocument) {
            return std::unexpected{std::move(validDocument.error())};
        }

        auto text = archive::writeJsonArchive(documentArchiveValue(document));
        if (!text) {
            return std::unexpected{
                amatIoError("Failed to write .amat JSON: " + text.error().message)};
        }

        return *text;
    }

    VoidResult writeAmatFile(const std::filesystem::path& path, const AmatDocument& document) {
        if (auto validDocument = validateAmatDocument(document); !validDocument) {
            return std::unexpected{std::move(validDocument.error())};
        }

        auto written = archive::writeJsonArchiveFile(path, documentArchiveValue(document));
        if (!written) {
            return std::unexpected{amatIoError("Failed to write .amat file '" + path.string() +
                                               "': " + written.error().message)};
        }
        return {};
    }

} // namespace asharia::material_instance
