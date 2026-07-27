#include "asharia/scene/world_native_api.h"

#include <cstddef>
#include <cstdint>
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

    static_assert(sizeof(AshariaSceneNativeStatus) == sizeof(std::uint32_t));
    static_assert(std::is_standard_layout_v<AshariaSceneNativeAbiHeader>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeAbiHeader>);
    static_assert(alignof(AshariaSceneNativeAbiHeader) == alignof(std::uint32_t));
    static_assert(sizeof(AshariaSceneNativeAbiHeader) == 8U);
    static_assert(offsetof(AshariaSceneNativeAbiHeader, abiVersion) == 0U);
    static_assert(offsetof(AshariaSceneNativeAbiHeader, structSize) == 4U);

    static_assert(std::is_standard_layout_v<AshariaSceneNativeWorldCreateRequest>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeWorldCreateRequest>);
    static_assert(alignof(AshariaSceneNativeWorldCreateRequest) ==
                  alignof(AshariaSceneNativeAbiHeader));
    static_assert(sizeof(AshariaSceneNativeWorldCreateRequest) ==
                  sizeof(AshariaSceneNativeAbiHeader));
    static_assert(offsetof(AshariaSceneNativeWorldCreateRequest, header) == 0U);
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

} // extern "C"
