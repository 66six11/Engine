#pragma once

#include <cstdint>
#include <string_view>

namespace asharia::asset {

    struct AssetTypeId {
        std::uint64_t value{};

        [[nodiscard]] friend bool operator==(AssetTypeId, AssetTypeId) = default;
        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return value != 0;
        }
    };

    namespace detail {
        inline constexpr std::uint64_t kFnv1a64Offset = 14695981039346656037ULL;
        inline constexpr std::uint64_t kFnv1a64Prime = 1099511628211ULL;

        [[nodiscard]] constexpr std::uint64_t hashAppend(std::uint64_t hash,
                                                         std::string_view text) noexcept {
            for (const char character : text) {
                hash ^= static_cast<unsigned char>(character);
                hash *= kFnv1a64Prime;
            }
            return hash;
        }
    } // namespace detail

    [[nodiscard]] constexpr AssetTypeId makeAssetTypeId(std::string_view typeName) noexcept {
        if (typeName.empty()) {
            return AssetTypeId{};
        }

        return AssetTypeId{detail::hashAppend(detail::kFnv1a64Offset, typeName)};
    }

} // namespace asharia::asset
