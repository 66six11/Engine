#include "asharia/scene/world_native_api.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <type_traits>

#include "asharia/scene/world.hpp"

struct AshariaSceneNativeWorld {
    asharia::World world;
    std::thread::id ownerThread;
};

namespace {

    [[nodiscard]] constexpr bool hasSupportedHeader(const AshariaSceneNativeAbiHeader& header,
                                                    std::size_t requiredSize) noexcept {
        return header.abiVersion == ASHARIA_SCENE_NATIVE_ABI_VERSION &&
               header.structSize >= requiredSize;
    }

    [[nodiscard]] bool isOwnerThread(const AshariaSceneNativeWorld& world) noexcept {
        return world.ownerThread == std::this_thread::get_id();
    }

    [[nodiscard]] constexpr asharia::EntityId
    toEntityId(AshariaSceneNativeEntityId entity) noexcept {
        return asharia::EntityId{.index = entity.index, .generation = entity.generation};
    }

    [[nodiscard]] constexpr AshariaSceneNativeEntityId
    fromEntityId(asharia::EntityId entity) noexcept {
        return AshariaSceneNativeEntityId{.index = entity.index, .generation = entity.generation};
    }

    [[nodiscard]] constexpr asharia::TransformComponent
    toLocalTransform(const AshariaSceneNativeTransform& transform) noexcept {
        return asharia::TransformComponent{
            .position =
                {
                    .x = transform.position.x,
                    .y = transform.position.y,
                    .z = transform.position.z,
                },
            .rotation =
                {
                    .x = transform.rotation.x,
                    .y = transform.rotation.y,
                    .z = transform.rotation.z,
                    .w = transform.rotation.w,
                },
            .scale =
                {
                    .x = transform.scale.x,
                    .y = transform.scale.y,
                    .z = transform.scale.z,
                },
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeTransform
    fromLocalTransform(const asharia::TransformComponent& transform) noexcept {
        return AshariaSceneNativeTransform{
            .position =
                {
                    .x = transform.position.x,
                    .y = transform.position.y,
                    .z = transform.position.z,
                },
            .rotation =
                {
                    .x = transform.rotation.x,
                    .y = transform.rotation.y,
                    .z = transform.rotation.z,
                    .w = transform.rotation.w,
                },
            .scale =
                {
                    .x = transform.scale.x,
                    .y = transform.scale.y,
                    .z = transform.scale.z,
                },
        };
    }

    [[nodiscard]] bool isFinite(const AshariaSceneNativeVec3& value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] bool isUnitQuaternion(const AshariaSceneNativeQuat& value) noexcept {
        if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
            !std::isfinite(value.w)) {
            return false;
        }

        constexpr double kUnitTolerance = 1.0e-3;
        const double componentX = value.x;
        const double componentY = value.y;
        const double componentZ = value.z;
        const double componentW = value.w;
        const double lengthSquared = (componentX * componentX) + (componentY * componentY) +
                                     (componentZ * componentZ) + (componentW * componentW);
        return std::abs(lengthSquared - 1.0) <= kUnitTolerance;
    }

    [[nodiscard]] bool
    isValidLocalTransform(const AshariaSceneNativeTransform& transform) noexcept {
        return isFinite(transform.position) && isUnitQuaternion(transform.rotation) &&
               isFinite(transform.scale);
    }

    static_assert(sizeof(AshariaSceneNativeStatus) == sizeof(std::uint32_t));
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);
    static_assert(std::is_standard_layout_v<AshariaSceneNativeAbiHeader>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeAbiHeader>);
    static_assert(alignof(AshariaSceneNativeAbiHeader) == alignof(std::uint32_t));
    static_assert(sizeof(AshariaSceneNativeAbiHeader) == 8U);
    static_assert(offsetof(AshariaSceneNativeAbiHeader, abiVersion) == 0U);
    static_assert(offsetof(AshariaSceneNativeAbiHeader, structSize) == 4U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeEntityId>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeEntityId>);
    static_assert(alignof(AshariaSceneNativeEntityId) == alignof(std::uint32_t));
    static_assert(sizeof(AshariaSceneNativeEntityId) == 8U);
    static_assert(offsetof(AshariaSceneNativeEntityId, index) == 0U);
    static_assert(offsetof(AshariaSceneNativeEntityId, generation) == 4U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeVec3>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeVec3>);
    static_assert(alignof(AshariaSceneNativeVec3) == alignof(float));
    static_assert(sizeof(AshariaSceneNativeVec3) == 12U);
    static_assert(offsetof(AshariaSceneNativeVec3, x) == 0U);
    static_assert(offsetof(AshariaSceneNativeVec3, y) == 4U);
    static_assert(offsetof(AshariaSceneNativeVec3, z) == 8U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeQuat>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeQuat>);
    static_assert(alignof(AshariaSceneNativeQuat) == alignof(float));
    static_assert(sizeof(AshariaSceneNativeQuat) == 16U);
    static_assert(offsetof(AshariaSceneNativeQuat, x) == 0U);
    static_assert(offsetof(AshariaSceneNativeQuat, y) == 4U);
    static_assert(offsetof(AshariaSceneNativeQuat, z) == 8U);
    static_assert(offsetof(AshariaSceneNativeQuat, w) == 12U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeTransform>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeTransform>);
    static_assert(alignof(AshariaSceneNativeTransform) == alignof(float));
    static_assert(sizeof(AshariaSceneNativeTransform) == 40U);
    static_assert(offsetof(AshariaSceneNativeTransform, position) == 0U);
    static_assert(offsetof(AshariaSceneNativeTransform, rotation) == 12U);
    static_assert(offsetof(AshariaSceneNativeTransform, scale) == 28U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeWorldCreateRequest>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeWorldCreateRequest>);
    static_assert(alignof(AshariaSceneNativeWorldCreateRequest) ==
                  alignof(AshariaSceneNativeAbiHeader));
    static_assert(sizeof(AshariaSceneNativeWorldCreateRequest) ==
                  sizeof(AshariaSceneNativeAbiHeader));
    static_assert(offsetof(AshariaSceneNativeWorldCreateRequest, header) == 0U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeCreateEntityRequest>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeCreateEntityRequest>);
    static_assert(alignof(AshariaSceneNativeCreateEntityRequest) ==
                  alignof(AshariaSceneNativeAbiHeader));
    static_assert(sizeof(AshariaSceneNativeCreateEntityRequest) ==
                  sizeof(AshariaSceneNativeAbiHeader));
    static_assert(offsetof(AshariaSceneNativeCreateEntityRequest, header) == 0U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeEntityRequest>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeEntityRequest>);
    static_assert(alignof(AshariaSceneNativeEntityRequest) == alignof(std::uint32_t));
    static_assert(sizeof(AshariaSceneNativeEntityRequest) == 16U);
    static_assert(offsetof(AshariaSceneNativeEntityRequest, header) == 0U);
    static_assert(offsetof(AshariaSceneNativeEntityRequest, entity) == 8U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeSetLocalTransformRequest>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeSetLocalTransformRequest>);
    static_assert(alignof(AshariaSceneNativeSetLocalTransformRequest) == alignof(float));
    static_assert(sizeof(AshariaSceneNativeSetLocalTransformRequest) == 56U);
    static_assert(offsetof(AshariaSceneNativeSetLocalTransformRequest, header) == 0U);
    static_assert(offsetof(AshariaSceneNativeSetLocalTransformRequest, entity) == 8U);
    static_assert(offsetof(AshariaSceneNativeSetLocalTransformRequest, transform) == 16U);
    static_assert(std::is_nothrow_destructible_v<AshariaSceneNativeWorld>);

} // namespace

extern "C" {

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_world_create(
    const AshariaSceneNativeWorldCreateRequest* request, AshariaSceneNativeWorld** world) noexcept {
    if (world == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *world = nullptr;

    if (request == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaSceneNativeWorldCreateRequest))) {
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }

    try {
        auto created = std::make_unique<AshariaSceneNativeWorld>();
        created->ownerThread = std::this_thread::get_id();

        // The opaque C handle intentionally transfers ownership to the caller.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        *world = created.release();
        return AshariaSceneNativeStatus_Success;
    } catch (...) {
        return AshariaSceneNativeStatus_InternalError;
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL
asharia_scene_world_destroy(AshariaSceneNativeWorld* world) noexcept {
    if (world == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!isOwnerThread(*world)) {
        return AshariaSceneNativeStatus_WrongThread;
    }

    // Reclaim the ownership transferred by asharia_scene_world_create.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    const std::unique_ptr<AshariaSceneNativeWorld> ownedWorld{world};
    return AshariaSceneNativeStatus_Success;
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_world_create_entity(
    AshariaSceneNativeWorld* world, const AshariaSceneNativeCreateEntityRequest* request,
    AshariaSceneNativeEntityId* entity) noexcept {
    if (entity == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *entity = {};

    if (world == nullptr || request == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaSceneNativeCreateEntityRequest))) {
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    if (!isOwnerThread(*world)) {
        return AshariaSceneNativeStatus_WrongThread;
    }

    try {
        auto created = world->world.createEntity();
        if (!created) {
            return AshariaSceneNativeStatus_EntityCapacityExceeded;
        }
        *entity = fromEntityId(*created);
        return AshariaSceneNativeStatus_Success;
    } catch (...) {
        return AshariaSceneNativeStatus_InternalError;
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_world_destroy_entity(
    AshariaSceneNativeWorld* world, const AshariaSceneNativeEntityRequest* request) noexcept {
    if (world == nullptr || request == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaSceneNativeEntityRequest))) {
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    if (!isOwnerThread(*world)) {
        return AshariaSceneNativeStatus_WrongThread;
    }

    try {
        return world->world.destroyEntity(toEntityId(request->entity))
                   ? AshariaSceneNativeStatus_Success
                   : AshariaSceneNativeStatus_InvalidEntity;
    } catch (...) {
        return AshariaSceneNativeStatus_InternalError;
    }
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_world_is_alive(
    AshariaSceneNativeWorld* world, const AshariaSceneNativeEntityRequest* request,
    std::uint32_t* isAlive) noexcept {
    if (isAlive == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *isAlive = 0U;

    if (world == nullptr || request == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaSceneNativeEntityRequest))) {
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    if (!isOwnerThread(*world)) {
        return AshariaSceneNativeStatus_WrongThread;
    }

    *isAlive = world->world.isAlive(toEntityId(request->entity)) ? 1U : 0U;
    return AshariaSceneNativeStatus_Success;
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_world_get_local_transform(
    AshariaSceneNativeWorld* world, const AshariaSceneNativeEntityRequest* request,
    AshariaSceneNativeTransform* transform) noexcept {
    if (transform == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    *transform = {};

    if (world == nullptr || request == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaSceneNativeEntityRequest))) {
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    if (!isOwnerThread(*world)) {
        return AshariaSceneNativeStatus_WrongThread;
    }

    const asharia::TransformComponent* localTransform =
        world->world.tryGetTransform(toEntityId(request->entity));
    if (localTransform == nullptr) {
        return AshariaSceneNativeStatus_InvalidEntity;
    }
    *transform = fromLocalTransform(*localTransform);
    return AshariaSceneNativeStatus_Success;
}

AshariaSceneNativeStatus ASHARIA_SCENE_NATIVE_CALL asharia_scene_world_set_local_transform(
    AshariaSceneNativeWorld* world,
    const AshariaSceneNativeSetLocalTransformRequest* request) noexcept {
    if (world == nullptr || request == nullptr) {
        return AshariaSceneNativeStatus_InvalidArgument;
    }
    if (!hasSupportedHeader(request->header, sizeof(AshariaSceneNativeSetLocalTransformRequest))) {
        return AshariaSceneNativeStatus_UnsupportedAbi;
    }
    if (!isOwnerThread(*world)) {
        return AshariaSceneNativeStatus_WrongThread;
    }
    if (!isValidLocalTransform(request->transform)) {
        return AshariaSceneNativeStatus_InvalidTransform;
    }

    try {
        return world->world.setTransform(toEntityId(request->entity),
                                         toLocalTransform(request->transform))
                   ? AshariaSceneNativeStatus_Success
                   : AshariaSceneNativeStatus_InvalidEntity;
    } catch (...) {
        return AshariaSceneNativeStatus_InternalError;
    }
}

} // extern "C"
