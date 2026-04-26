#pragma once

#include <array>
#include <cstdint>

namespace vke {

    struct BasicVertex {
        float position[2]{};
        float color[3]{};
    };

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

    [[nodiscard]] constexpr std::array<BasicVertex, 3> basicTriangleVertices() {
        return std::array{
            BasicVertex{
                .position = {0.0F, -0.55F},
                .color = {0.95F, 0.24F, 0.18F},
            },
            BasicVertex{
                .position = {0.55F, 0.45F},
                .color = {0.12F, 0.72F, 0.36F},
            },
            BasicVertex{
                .position = {-0.55F, 0.45F},
                .color = {0.18F, 0.38F, 0.95F},
            },
        };
    }

} // namespace vke
