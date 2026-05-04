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
        std::uint32_t indexCount{};
        std::uint32_t instanceCount{1};
        std::uint32_t firstVertex{};
        std::uint32_t firstIndex{};
        std::int32_t vertexOffset{};
        std::uint32_t firstInstance{};
    };

    enum class BasicMeshKind {
        Triangle,
        IndexedQuad,
    };

    [[nodiscard]] constexpr BasicDrawItem basicTriangleDrawItem() {
        return BasicDrawItem{
            .vertexCount = 3,
            .indexCount = 0,
            .instanceCount = 1,
            .firstVertex = 0,
            .firstIndex = 0,
            .vertexOffset = 0,
            .firstInstance = 0,
        };
    }

    [[nodiscard]] constexpr BasicDrawItem basicIndexedQuadDrawItem() {
        return BasicDrawItem{
            .vertexCount = 0,
            .indexCount = 6,
            .instanceCount = 1,
            .firstVertex = 0,
            .firstIndex = 0,
            .vertexOffset = 0,
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

    [[nodiscard]] constexpr std::array<BasicVertex, 4> basicQuadVertices() {
        return std::array{
            BasicVertex{
                .position = {-0.55F, -0.45F},
                .color = {0.95F, 0.24F, 0.18F},
            },
            BasicVertex{
                .position = {0.55F, -0.45F},
                .color = {0.12F, 0.72F, 0.36F},
            },
            BasicVertex{
                .position = {0.55F, 0.45F},
                .color = {0.18F, 0.38F, 0.95F},
            },
            BasicVertex{
                .position = {-0.55F, 0.45F},
                .color = {0.95F, 0.82F, 0.18F},
            },
        };
    }

    [[nodiscard]] constexpr std::array<std::uint16_t, 6> basicQuadIndices() {
        return std::array<std::uint16_t, 6>{0, 1, 2, 2, 3, 0};
    }

} // namespace vke
