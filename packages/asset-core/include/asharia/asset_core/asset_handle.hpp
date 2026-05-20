#pragma once

#include "asharia/asset_core/asset_guid.hpp"

namespace asharia::asset {

    template <class T>
    struct AssetHandle {
        AssetGuid guid{};

        [[nodiscard]] friend bool operator==(AssetHandle, AssetHandle) = default;
        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(guid);
        }
    };

} // namespace asharia::asset
