#pragma once

#include "asharia/core/result.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace asharia::asset {

    struct AssetGuid {
        std::array<std::uint8_t, 16> bytes{};

        [[nodiscard]] friend bool operator==(AssetGuid, AssetGuid) = default;
        [[nodiscard]] explicit operator bool() const noexcept;
    };

    [[nodiscard]] Result<AssetGuid> parseAssetGuid(std::string_view text);
    [[nodiscard]] std::string formatAssetGuid(AssetGuid guid);

} // namespace asharia::asset
