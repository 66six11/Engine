#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/shader_authoring/ashader_document.hpp"

namespace asharia::material_instance {

    inline constexpr std::uint32_t kAmatSchemaVersion = 2;

    enum class AmatPropertyValueKind {
        Number,
        Integer,
        UnsignedInteger,
        Boolean,
        Vector,
        AssetGuid,
    };

    struct AmatPropertyValue {
        AmatPropertyValueKind kind{AmatPropertyValueKind::Number};
        double numberValue{};
        std::int64_t integerValue{};
        std::uint64_t unsignedIntegerValue{};
        bool boolValue{};
        std::vector<double> vectorValue;
        asset::AssetGuid assetGuid{};

        [[nodiscard]] friend bool operator==(const AmatPropertyValue&,
                                             const AmatPropertyValue&) = default;
    };

    struct AmatMaterialTypeReference {
        asset::AssetGuid assetGuid{};
        std::string stableTypeId;
        std::uint64_t expectedTypeHash{};

        [[nodiscard]] friend bool operator==(const AmatMaterialTypeReference&,
                                             const AmatMaterialTypeReference&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(assetGuid) && !stableTypeId.empty() && expectedTypeHash != 0;
        }
    };

    struct AmatPropertyOverride {
        std::string propertyId;
        shader_authoring::AshaderPropertyType type{shader_authoring::AshaderPropertyType::Float};
        AmatPropertyValue value;

        [[nodiscard]] friend bool operator==(const AmatPropertyOverride&,
                                             const AmatPropertyOverride&) = default;
    };

    struct AmatImportMetadata {
        std::uint64_t lastCookedSignatureHash{};
        std::string lastCookedAt;

        [[nodiscard]] friend bool operator==(const AmatImportMetadata&,
                                             const AmatImportMetadata&) = default;
    };

    struct AmatDocument {
        std::uint32_t schemaVersion{kAmatSchemaVersion};
        AmatMaterialTypeReference materialType;
        std::vector<AmatPropertyOverride> properties;
        AmatImportMetadata import;

        [[nodiscard]] friend bool operator==(const AmatDocument&, const AmatDocument&) = default;
    };

    [[nodiscard]] std::string_view toString(AmatPropertyValueKind kind) noexcept;

} // namespace asharia::material_instance
