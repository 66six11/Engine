#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/shader_authoring/ashader_document.hpp"

namespace asharia::material_instance {

    inline constexpr std::uint32_t kMatSchemaVersion = 2;

    enum class MatPropertyValueKind {
        Number,
        Integer,
        UnsignedInteger,
        Boolean,
        Vector,
        AssetGuid,
    };

    struct MatPropertyValue {
        MatPropertyValueKind kind{MatPropertyValueKind::Number};
        double numberValue{};
        std::int64_t integerValue{};
        std::uint64_t unsignedIntegerValue{};
        bool boolValue{};
        std::vector<double> vectorValue;
        asset::AssetGuid assetGuid{};

        [[nodiscard]] friend bool operator==(const MatPropertyValue&,
                                             const MatPropertyValue&) = default;
    };

    struct MatMaterialTypeReference {
        asset::AssetGuid assetGuid{};
        std::string stableTypeId;
        std::uint64_t expectedTypeHash{};

        [[nodiscard]] friend bool operator==(const MatMaterialTypeReference&,
                                             const MatMaterialTypeReference&) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(assetGuid) && !stableTypeId.empty() && expectedTypeHash != 0;
        }
    };

    struct MatPropertyOverride {
        std::string propertyId;
        shader_authoring::AshaderPropertyType type{shader_authoring::AshaderPropertyType::Float};
        MatPropertyValue value;

        [[nodiscard]] friend bool operator==(const MatPropertyOverride&,
                                             const MatPropertyOverride&) = default;
    };

    struct MatImportMetadata {
        std::uint64_t lastCookedSignatureHash{};
        std::string lastCookedAt;

        [[nodiscard]] friend bool operator==(const MatImportMetadata&,
                                             const MatImportMetadata&) = default;
    };

    struct MatDocument {
        std::uint32_t schemaVersion{kMatSchemaVersion};
        MatMaterialTypeReference materialType;
        std::vector<MatPropertyOverride> properties;
        MatImportMetadata import;

        [[nodiscard]] friend bool operator==(const MatDocument&, const MatDocument&) = default;
    };

    [[nodiscard]] std::string_view toString(MatPropertyValueKind kind) noexcept;

} // namespace asharia::material_instance
