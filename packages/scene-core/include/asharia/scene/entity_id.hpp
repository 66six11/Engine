#pragma once

#include <cstdint>

namespace asharia {

    struct EntityId {
        std::uint32_t index{};
        std::uint32_t generation{};

        [[nodiscard]] friend constexpr bool operator==(EntityId, EntityId) = default;

        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return index != 0U && generation != 0U;
        }
    };

    inline constexpr EntityId kInvalidEntityId{};

    [[nodiscard]] constexpr bool isValid(EntityId entity) noexcept {
        return static_cast<bool>(entity);
    }

} // namespace asharia
