#include "editor_viewport.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
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

        constexpr float kEditorViewportClipNearZ = 0.0F;
        constexpr float kEditorViewportClipFarZ = 1.0F;
        constexpr float kEditorViewportUnprojectEpsilon = 0.000001F;

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

        [[nodiscard]] glm::mat4 editorViewportMatrixToGlm(const EditorViewportMatrix4x4& matrix) {
            glm::mat4 result{1.0F};
            for (std::size_t row = 0; row < 4U; ++row) {
                for (std::size_t column = 0; column < 4U; ++column) {
                    result[static_cast<glm::mat4::length_type>(column)]
                          [static_cast<glm::mat4::length_type>(row)] =
                              editorViewportMatrixAt(matrix, row, column);
                }
            }
            return result;
        }

        [[nodiscard]] bool finiteGlmVec3(glm::vec3 value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] std::array<float, 3> editorVec3FromGlm(glm::vec3 value) {
            return std::array<float, 3>{value.x, value.y, value.z};
        }

        [[nodiscard]] std::optional<glm::vec3>
        editorViewportClipPointToWorld(const glm::mat4& inverseViewProjection,
                                       glm::vec4 clipPoint) {
            const glm::vec4 worldPoint = inverseViewProjection * clipPoint;
            if (!std::isfinite(worldPoint.w) ||
                std::fabs(worldPoint.w) <= kEditorViewportUnprojectEpsilon) {
                return std::nullopt;
            }

            const glm::vec3 dividedPoint =
                glm::vec3{worldPoint.x, worldPoint.y, worldPoint.z} / worldPoint.w;
            if (!finiteGlmVec3(dividedPoint)) {
                return std::nullopt;
            }
            return dividedPoint;
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
        if (!isRenderableEditorExtent(extent) || camera.farPlane <= camera.nearPlane) {
            return std::nullopt;
        }

        const glm::mat4 viewProjection = editorViewportMatrixToGlm(camera.viewProjection);
        const float determinant = glm::determinant(viewProjection);
        if (!std::isfinite(determinant) ||
            std::fabs(determinant) <= kEditorViewportUnprojectEpsilon) {
            return std::nullopt;
        }

        const auto width = static_cast<float>(extent.width);
        const auto height = static_cast<float>(extent.height);
        const float ndcX = (2.0F * point.x / width) - 1.0F;
        const float ndcY = 1.0F - (2.0F * point.y / height);

        const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
        const std::optional<glm::vec3> nearPoint = editorViewportClipPointToWorld(
            inverseViewProjection, glm::vec4{ndcX, ndcY, kEditorViewportClipNearZ, 1.0F});
        const std::optional<glm::vec3> farPoint = editorViewportClipPointToWorld(
            inverseViewProjection, glm::vec4{ndcX, ndcY, kEditorViewportClipFarZ, 1.0F});
        if (!nearPoint || !farPoint) {
            return std::nullopt;
        }

        const glm::vec3 rayDelta = *farPoint - *nearPoint;
        const float rayLengthSquared = glm::dot(rayDelta, rayDelta);
        if (!std::isfinite(rayLengthSquared) ||
            rayLengthSquared <= kEditorViewportUnprojectEpsilon) {
            return std::nullopt;
        }
        const glm::vec3 direction = glm::normalize(rayDelta);
        if (!finiteGlmVec3(direction)) {
            return std::nullopt;
        }

        return EditorViewportWorldRay{
            .origin = editorVec3FromGlm(*nearPoint),
            .nearPoint = editorVec3FromGlm(*nearPoint),
            .farPoint = editorVec3FromGlm(*farPoint),
            .direction = editorVec3FromGlm(direction),
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

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void orbitEditorViewportCamera(EditorViewportCamera& camera, float deltaYaw, float deltaPitch) {
        const EditorVec3 direction =
            normalizeEditorVec3(subtractEditorVec3(camera.position, camera.target));
        float currentYaw = std::atan2(direction[0], direction[2]);
        float currentPitch = std::asin(direction[1]);
        currentYaw -= deltaYaw;
        currentPitch = std::clamp(currentPitch - deltaPitch, -1.5F, 1.5F);
        const float distance = std::sqrt(dotEditorVec3(
            subtractEditorVec3(camera.position, camera.target),
            subtractEditorVec3(camera.position, camera.target)));
        const float newX =
            camera.target[0] + (distance * std::cos(currentPitch) * std::sin(currentYaw));
        const float newY = camera.target[1] + (distance * std::sin(currentPitch));
        const float newZ =
            camera.target[2] + (distance * std::cos(currentPitch) * std::cos(currentYaw));
        camera.position = EditorVec3{newX, newY, newZ};
    }

    void panEditorViewportCamera(EditorViewportCamera& camera, float deltaX, float deltaY,
                                 [[maybe_unused]] EditorExtent2D extent) {
        const EditorVec3 forward =
            normalizeEditorVec3(subtractEditorVec3(camera.target, camera.position));
        const EditorVec3 right =
            normalizeEditorVec3(crossEditorVec3(forward, camera.up));
        const EditorVec3 viewUp =
            normalizeEditorVec3(crossEditorVec3(right, forward));
        const float distanceToTarget = std::sqrt(dotEditorVec3(
            subtractEditorVec3(camera.target, camera.position),
            subtractEditorVec3(camera.target, camera.position)));
        const float panScale = distanceToTarget * 0.0025F;
        const float panX = -deltaX * panScale;
        const float panY = deltaY * panScale;
        const EditorVec3 offset{
            (right[0] * panX) + (viewUp[0] * panY),
            (right[1] * panX) + (viewUp[1] * panY),
            (right[2] * panX) + (viewUp[2] * panY),
        };
        camera.position = EditorVec3{
            camera.position[0] + offset[0],
            camera.position[1] + offset[1],
            camera.position[2] + offset[2],
        };
        camera.target = EditorVec3{
            camera.target[0] + offset[0],
            camera.target[1] + offset[1],
            camera.target[2] + offset[2],
        };
    }

    void dollyEditorViewportCamera(EditorViewportCamera& camera, float delta) {
        const float distance = std::sqrt(dotEditorVec3(
            subtractEditorVec3(camera.position, camera.target),
            subtractEditorVec3(camera.position, camera.target)));
        const float newDistance = std::max(0.1F, distance - (delta * 0.5F));
        const float ratio = newDistance / std::max(distance, 0.001F);
        camera.position = EditorVec3{
            camera.target[0] + ((camera.position[0] - camera.target[0]) * ratio),
            camera.target[1] + ((camera.position[1] - camera.target[1]) * ratio),
            camera.target[2] + ((camera.position[2] - camera.target[2]) * ratio),
        };
    }

} // namespace asharia::editor
