#pragma once

#include "asharia/asset_core/asset_guid.hpp"
#include "asharia/asset_core/asset_type.hpp"
#include "asharia/core/result.hpp"

#include <string_view>

namespace asharia::asset {

    struct AssetReference {
        AssetGuid guid{};
        AssetTypeId expectedType{};

        [[nodiscard]] friend bool operator==(AssetReference, AssetReference) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(guid) && static_cast<bool>(expectedType);
        }
    };

    [[nodiscard]] constexpr AssetReference makeAssetReference(AssetGuid guid,
                                                             AssetTypeId expectedType) noexcept {
        return AssetReference{
            .guid = guid,
            .expectedType = expectedType,
        };
    }

    [[nodiscard]] VoidResult validateAssetReference(AssetReference reference,
                                                    AssetTypeId actualType,
                                                    std::string_view sourcePath,
                                                    std::string_view expectedTypeName,
                                                    std::string_view actualTypeName);

} // namespace asharia::asset
