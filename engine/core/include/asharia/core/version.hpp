#pragma once

#include <cstdint>
#include <string_view>

namespace asharia {

struct Version {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};
};

inline constexpr Version kEngineVersion{0, 1, 0};
inline constexpr std::string_view kEngineName{"Asharia Engine"};

} // namespace asharia
