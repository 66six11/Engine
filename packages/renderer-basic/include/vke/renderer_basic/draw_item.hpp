#pragma once

#include <array>
#include <cstdint>

namespace vke {

    struct BasicVertex {
        float position[2]{};
        float color[3]{};
    };

    struct BasicVertex3D {
        float position[3]{};
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

    using BasicTransformMatrix3D = std::array<float, 16>;

    struct BasicDrawListItem {
        BasicDrawItem drawItem{};
        BasicTransformMatrix3D modelMatrix{};
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

    [[nodiscard]] constexpr BasicDrawItem basicIndexedCubeDrawItem() {
        return BasicDrawItem{
            .vertexCount = 0,
            .indexCount = 36,
            .instanceCount = 1,
            .firstVertex = 0,
            .firstIndex = 0,
            .vertexOffset = 0,
            .firstInstance = 0,
        };
    }

    [[nodiscard]] constexpr BasicTransformMatrix3D basicIdentityTransform3D() {
        return BasicTransformMatrix3D{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        };
    }

    [[nodiscard]] constexpr BasicTransformMatrix3D
    basicTranslationTransform3D(float x, float y, float z) {
        return BasicTransformMatrix3D{
            1.0F, 0.0F, 0.0F, x,
            0.0F, 1.0F, 0.0F, y,
            0.0F, 0.0F, 1.0F, z,
            0.0F, 0.0F, 0.0F, 1.0F,
        };
    }

    [[nodiscard]] constexpr std::array<BasicDrawListItem, 2> basicDrawListSmokeItems() {
        return std::array{
            BasicDrawListItem{
                .drawItem = basicIndexedCubeDrawItem(),
                .modelMatrix = basicTranslationTransform3D(-0.72F, 0.0F, 3.0F),
            },
            BasicDrawListItem{
                .drawItem = basicIndexedCubeDrawItem(),
                .modelMatrix = basicTranslationTransform3D(0.72F, 0.0F, 3.0F),
            },
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

    [[nodiscard]] constexpr std::array<BasicVertex3D, 8> basicCubeVertices() {
        return std::array{
            BasicVertex3D{
                .position = {-0.55F, -0.55F, -0.55F},
                .color = {0.95F, 0.24F, 0.18F},
            },
            BasicVertex3D{
                .position = {0.55F, -0.55F, -0.55F},
                .color = {0.12F, 0.72F, 0.36F},
            },
            BasicVertex3D{
                .position = {0.55F, 0.55F, -0.55F},
                .color = {0.18F, 0.38F, 0.95F},
            },
            BasicVertex3D{
                .position = {-0.55F, 0.55F, -0.55F},
                .color = {0.95F, 0.82F, 0.18F},
            },
            BasicVertex3D{
                .position = {-0.55F, -0.55F, 0.55F},
                .color = {0.10F, 0.78F, 0.82F},
            },
            BasicVertex3D{
                .position = {0.55F, -0.55F, 0.55F},
                .color = {0.86F, 0.32F, 0.72F},
            },
            BasicVertex3D{
                .position = {0.55F, 0.55F, 0.55F},
                .color = {0.90F, 0.48F, 0.18F},
            },
            BasicVertex3D{
                .position = {-0.55F, 0.55F, 0.55F},
                .color = {0.55F, 0.90F, 0.28F},
            },
        };
    }

    [[nodiscard]] constexpr std::array<std::uint16_t, 36> basicCubeIndices() {
        return std::array<std::uint16_t, 36>{
            0, 1, 2, 2, 3, 0, 4, 6, 5, 6, 4, 7, 0, 4, 5, 5, 1, 0,
            3, 2, 6, 6, 7, 3, 1, 5, 6, 6, 2, 1, 0, 3, 7, 7, 4, 0,
        };
    }

} // namespace vke
