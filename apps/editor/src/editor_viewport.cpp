#include "editor_viewport.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace asharia::editor {

    namespace {

        using EditorVec3 = std::array<float, 3>;

        struct EditorViewportLookAtDesc {
            EditorVec3 position{};
            EditorVec3 target{};
            EditorVec3 cameraUpHint{};
        };

        struct EditorViewportPerspectiveDesc {
            float verticalFovRadians{};
            float aspectRatio{1.0F};
            float nearPlane{0.1F};
            float farPlane{1000.0F};
        };

        [[nodiscard]] constexpr float editorViewportMatrixAt(const EditorViewportMatrix4x4& matrix,
                                                             std::size_t row, std::size_t column) {
            return matrix.at((row * 4U) + column);
        }

        [[nodiscard]] EditorViewportMatrix4x4
        multiplyEditorViewportMatrices(const EditorViewportMatrix4x4& lhs,
                                       const EditorViewportMatrix4x4& rhs) {
            EditorViewportMatrix4x4 result{};
            for (std::size_t row = 0; row < 4U; ++row) {
                for (std::size_t column = 0; column < 4U; ++column) {
                    float value = 0.0F;
                    for (std::size_t index = 0; index < 4U; ++index) {
                        value += editorViewportMatrixAt(lhs, row, index) *
                                 editorViewportMatrixAt(rhs, index, column);
                    }
                    result.at((row * 4U) + column) = value;
                }
            }
            return result;
        }

        [[nodiscard]] constexpr EditorVec3 subtractEditorVec3(EditorVec3 lhs, EditorVec3 rhs) {
            return EditorVec3{
                lhs[0] - rhs[0],
                lhs[1] - rhs[1],
                lhs[2] - rhs[2],
            };
        }

        [[nodiscard]] constexpr EditorVec3 addEditorVec3(EditorVec3 lhs, EditorVec3 rhs) {
            return EditorVec3{
                lhs[0] + rhs[0],
                lhs[1] + rhs[1],
                lhs[2] + rhs[2],
            };
        }

        [[nodiscard]] constexpr EditorVec3 scaleEditorVec3(EditorVec3 value, float scale) {
            return EditorVec3{
                value[0] * scale,
                value[1] * scale,
                value[2] * scale,
            };
        }

        [[nodiscard]] constexpr float dotEditorVec3(EditorVec3 lhs, EditorVec3 rhs) {
            return (lhs[0] * rhs[0]) + (lhs[1] * rhs[1]) + (lhs[2] * rhs[2]);
        }

        [[nodiscard]] constexpr EditorVec3 crossEditorVec3(EditorVec3 lhs, EditorVec3 rhs) {
            return EditorVec3{
                (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]),
                (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]),
                (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]),
            };
        }

        [[nodiscard]] EditorVec3 normalizeEditorVec3(EditorVec3 value) {
            const float length = std::sqrt(dotEditorVec3(value, value));
            if (length <= 0.0F) {
                return EditorVec3{};
            }
            return EditorVec3{value[0] / length, value[1] / length, value[2] / length};
        }

        [[nodiscard]] constexpr bool hasEditorVec3Direction(EditorVec3 value) {
            return dotEditorVec3(value, value) > 0.0F;
        }

        [[nodiscard]] EditorViewportMatrix4x4
        editorViewportLookAtMatrix(const EditorViewportLookAtDesc& desc) {
            const EditorVec3 forward =
                normalizeEditorVec3(subtractEditorVec3(desc.target, desc.position));
            const EditorVec3 right =
                normalizeEditorVec3(crossEditorVec3(desc.cameraUpHint, forward));
            const EditorVec3 cameraUp = crossEditorVec3(forward, right);

            return EditorViewportMatrix4x4{
                right[0],    right[1],    right[2],    -dotEditorVec3(right, desc.position),
                cameraUp[0], cameraUp[1], cameraUp[2], -dotEditorVec3(cameraUp, desc.position),
                forward[0],  forward[1],  forward[2],  -dotEditorVec3(forward, desc.position),
                0.0F,        0.0F,        0.0F,        1.0F,
            };
        }

        [[nodiscard]] EditorViewportMatrix4x4
        editorViewportPerspectiveMatrix(const EditorViewportPerspectiveDesc& desc) {
            const float focalLength = 1.0F / std::tan(desc.verticalFovRadians * 0.5F);
            return EditorViewportMatrix4x4{
                focalLength / desc.aspectRatio,
                0.0F,
                0.0F,
                0.0F,
                0.0F,
                focalLength,
                0.0F,
                0.0F,
                0.0F,
                0.0F,
                desc.farPlane / (desc.farPlane - desc.nearPlane),
                (-desc.nearPlane * desc.farPlane) / (desc.farPlane - desc.nearPlane),
                0.0F,
                0.0F,
                1.0F,
                0.0F,
            };
        }

    } // namespace

    bool isRenderableEditorExtent(EditorExtent2D extent) {
        return extent.width > 0 && extent.height > 0;
    }

    EditorViewportCamera defaultEditorSceneViewCamera(EditorExtent2D extent) {
        constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
        EditorViewportCamera camera{
            .view = editorViewportIdentityMatrix(),
            .projection = editorViewportIdentityMatrix(),
            .viewProjection = editorViewportIdentityMatrix(),
            .position = {0.0F, 2.0F, -6.0F},
            .target = {0.0F, 0.0F, 0.0F},
            .up = {0.0F, 1.0F, 0.0F},
            .verticalFovRadians = 60.0F * kDegreesToRadians,
            .aspectRatio = 1.0F,
            .nearPlane = 0.1F,
            .farPlane = 1000.0F,
        };
        return editorViewportCameraForExtent(camera, extent);
    }

    EditorViewportCamera editorViewportCameraForExtent(const EditorViewportCamera& camera,
                                                       EditorExtent2D extent) {
        const float width = static_cast<float>(std::max(extent.width, 1U));
        const float height = static_cast<float>(std::max(extent.height, 1U));
        EditorViewportCamera updated = camera;
        updated.aspectRatio = width / height;
        updated.view = editorViewportLookAtMatrix(EditorViewportLookAtDesc{
            .position = updated.position,
            .target = updated.target,
            .cameraUpHint = updated.up,
        });
        updated.projection = editorViewportPerspectiveMatrix(EditorViewportPerspectiveDesc{
            .verticalFovRadians = updated.verticalFovRadians,
            .aspectRatio = updated.aspectRatio,
            .nearPlane = updated.nearPlane,
            .farPlane = updated.farPlane,
        });
        updated.viewProjection = multiplyEditorViewportMatrices(updated.projection, updated.view);
        return updated;
    }

    std::optional<EditorViewportWorldRay>
    unprojectEditorViewportPoint(const EditorViewportCamera& camera, EditorExtent2D extent,
                                 EditorViewportPoint point) {
        if (!isRenderableEditorExtent(extent) || camera.verticalFovRadians <= 0.0F ||
            camera.aspectRatio <= 0.0F || camera.farPlane <= camera.nearPlane) {
            return std::nullopt;
        }

        const EditorVec3 forward =
            normalizeEditorVec3(subtractEditorVec3(camera.target, camera.position));
        const EditorVec3 right = normalizeEditorVec3(crossEditorVec3(camera.up, forward));
        if (!hasEditorVec3Direction(forward) || !hasEditorVec3Direction(right)) {
            return std::nullopt;
        }
        const EditorVec3 cameraUp = crossEditorVec3(forward, right);

        const float width = static_cast<float>(extent.width);
        const float height = static_cast<float>(extent.height);
        const float ndcX = (2.0F * point.x / width) - 1.0F;
        const float ndcY = 1.0F - (2.0F * point.y / height);
        const float tanHalfFov = std::tan(camera.verticalFovRadians * 0.5F);
        const EditorVec3 direction = normalizeEditorVec3(addEditorVec3(
            addEditorVec3(forward, scaleEditorVec3(right, ndcX * camera.aspectRatio * tanHalfFov)),
            scaleEditorVec3(cameraUp, ndcY * tanHalfFov)));
        if (!hasEditorVec3Direction(direction)) {
            return std::nullopt;
        }

        return EditorViewportWorldRay{
            .origin = camera.position,
            .direction = direction,
        };
    }

    bool hasEditorViewportTexture(const EditorViewportTexture& texture) {
        return texture.textureId != 0 && isRenderableEditorExtent(texture.extent);
    }

    bool anyEditorViewportOverlayFlagEnabled(EditorViewportOverlayFlags flags) {
        return flags.gridVisible || flags.gizmoVisible || flags.wireVisible ||
               flags.selectionOutlineVisible || flags.debugOverlayVisible ||
               flags.debugGizmoVisible;
    }

    bool anyEditorSceneOnlyOverlayFlagEnabled(EditorViewportOverlayFlags flags) {
        return flags.gridVisible || flags.gizmoVisible || flags.wireVisible ||
               flags.selectionOutlineVisible;
    }

    bool sameEditorViewportOverlayFlags(EditorViewportOverlayFlags lhs,
                                        EditorViewportOverlayFlags rhs) {
        return lhs.gridVisible == rhs.gridVisible && lhs.gizmoVisible == rhs.gizmoVisible &&
               lhs.wireVisible == rhs.wireVisible &&
               lhs.selectionOutlineVisible == rhs.selectionOutlineVisible &&
               lhs.debugOverlayVisible == rhs.debugOverlayVisible &&
               lhs.debugGizmoVisible == rhs.debugGizmoVisible;
    }

    EditorViewportOverlayFlags
    effectiveEditorViewportOverlayFlags(EditorViewportKind kind, EditorViewportOverlayFlags flags) {
        if (kind == EditorViewportKind::Scene) {
            return flags;
        }
        if (kind == EditorViewportKind::Game) {
            return EditorViewportOverlayFlags{
                .debugOverlayVisible = flags.debugOverlayVisible,
                .debugGizmoVisible = flags.debugGizmoVisible,
            };
        }
        if (kind == EditorViewportKind::Preview) {
            return {};
        }
        return {};
    }

} // namespace asharia::editor
