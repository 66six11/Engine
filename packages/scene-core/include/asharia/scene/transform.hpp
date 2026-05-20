#pragma once

namespace asharia {

    struct Vec3 {
        float x{};
        float y{};
        float z{};

        [[nodiscard]] friend constexpr bool operator==(Vec3, Vec3) = default;
    };

    struct Quat {
        float x{};
        float y{};
        float z{};
        float w{1.0F};

        [[nodiscard]] friend constexpr bool operator==(Quat, Quat) = default;
    };

    struct TransformComponent {
        Vec3 position{};
        Quat rotation{};
        Vec3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};

        [[nodiscard]] friend constexpr bool operator==(const TransformComponent&,
                                                       const TransformComponent&) = default;
    };

} // namespace asharia
