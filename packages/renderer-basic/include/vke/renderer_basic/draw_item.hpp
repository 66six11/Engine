#pragma once

#include <cstdint>

namespace vke {

    struct BasicDrawItem {
        std::uint32_t vertexCount{};
        std::uint32_t instanceCount{1};
        std::uint32_t firstVertex{};
        std::uint32_t firstInstance{};
    };

    [[nodiscard]] constexpr BasicDrawItem basicTriangleDrawItem() {
        return BasicDrawItem{
            .vertexCount = 3,
            .instanceCount = 1,
            .firstVertex = 0,
            .firstInstance = 0,
        };
    }

} // namespace vke
