#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <thread>

#include "asharia/scene/world_native_api.h"

namespace {

    [[nodiscard]] constexpr AshariaSceneNativeWorldCreateRequest worldCreateRequest() noexcept {
        return AshariaSceneNativeWorldCreateRequest{
            .header =
                {
                    .abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION,
                    .structSize =
                        static_cast<std::uint32_t>(sizeof(AshariaSceneNativeWorldCreateRequest)),
                },
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeCreateEntityRequest createEntityRequest() noexcept {
        return AshariaSceneNativeCreateEntityRequest{
            .header =
                {
                    .abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION,
                    .structSize =
                        static_cast<std::uint32_t>(sizeof(AshariaSceneNativeCreateEntityRequest)),
                },
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeEntityRequest
    entityRequest(AshariaSceneNativeEntityId entity) noexcept {
        return AshariaSceneNativeEntityRequest{
            .header =
                {
                    .abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION,
                    .structSize =
                        static_cast<std::uint32_t>(sizeof(AshariaSceneNativeEntityRequest)),
                },
            .entity = entity,
        };
    }

    [[nodiscard]] constexpr bool isZero(AshariaSceneNativeEntityId entity) noexcept {
        return entity.index == 0U && entity.generation == 0U;
    }

    [[nodiscard]] bool expectStatus(AshariaSceneNativeStatus actual,
                                    AshariaSceneNativeStatus expected, std::string_view operation) {
        if (actual == expected) {
            return true;
        }

        std::cerr << operation << " returned status " << actual << ", expected " << expected
                  << ".\n";
        return false;
    }

    [[nodiscard]] bool smokeEntityInvalidArgumentsAndAbi(AshariaSceneNativeWorld* world) {
        const auto validCreateRequest = createEntityRequest();
        AshariaSceneNativeEntityId entity{.index = 7U, .generation = 9U};
        if (!expectStatus(asharia_scene_world_create_entity(nullptr, &validCreateRequest, &entity),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "entity create with null world") ||
            !isZero(entity)) {
            return false;
        }

        entity = {.index = 7U, .generation = 9U};
        if (!expectStatus(asharia_scene_world_create_entity(world, nullptr, &entity),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "entity create with null request") ||
            !isZero(entity) ||
            !expectStatus(asharia_scene_world_create_entity(world, &validCreateRequest, nullptr),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "entity create with null output")) {
            return false;
        }

        auto unsupportedCreateRequest = validCreateRequest;
        unsupportedCreateRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION + 1U;
        entity = {.index = 7U, .generation = 9U};
        if (!expectStatus(
                asharia_scene_world_create_entity(world, &unsupportedCreateRequest, &entity),
                AshariaSceneNativeStatus_UnsupportedAbi, "entity create with unsupported ABI") ||
            !isZero(entity)) {
            return false;
        }

        auto undersizedCreateRequest = validCreateRequest;
        undersizedCreateRequest.header.structSize =
            static_cast<std::uint32_t>(sizeof(AshariaSceneNativeAbiHeader) - 1U);
        entity = {.index = 7U, .generation = 9U};
        if (!expectStatus(
                asharia_scene_world_create_entity(world, &undersizedCreateRequest, &entity),
                AshariaSceneNativeStatus_UnsupportedAbi, "entity create with undersized request") ||
            !isZero(entity)) {
            return false;
        }

        const auto validEntityRequest =
            entityRequest(AshariaSceneNativeEntityId{.index = 1U, .generation = 1U});
        if (!expectStatus(asharia_scene_world_destroy_entity(nullptr, &validEntityRequest),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "entity destroy with null world") ||
            !expectStatus(asharia_scene_world_destroy_entity(world, nullptr),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "entity destroy with null request")) {
            return false;
        }

        auto unsupportedEntityRequest = validEntityRequest;
        unsupportedEntityRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION + 1U;
        if (!expectStatus(asharia_scene_world_destroy_entity(world, &unsupportedEntityRequest),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "entity destroy with unsupported ABI")) {
            return false;
        }

        auto undersizedEntityRequest = validEntityRequest;
        undersizedEntityRequest.header.structSize =
            static_cast<std::uint32_t>(sizeof(AshariaSceneNativeEntityRequest) - 1U);
        if (!expectStatus(asharia_scene_world_destroy_entity(world, &undersizedEntityRequest),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "entity destroy with undersized request")) {
            return false;
        }

        std::uint32_t isAlive = 7U;
        if (!expectStatus(asharia_scene_world_is_alive(nullptr, &validEntityRequest, &isAlive),
                          AshariaSceneNativeStatus_InvalidArgument, "is alive with null world") ||
            isAlive != 0U) {
            return false;
        }

        isAlive = 7U;
        if (!expectStatus(asharia_scene_world_is_alive(world, nullptr, &isAlive),
                          AshariaSceneNativeStatus_InvalidArgument, "is alive with null request") ||
            isAlive != 0U ||
            !expectStatus(asharia_scene_world_is_alive(world, &validEntityRequest, nullptr),
                          AshariaSceneNativeStatus_InvalidArgument, "is alive with null output")) {
            return false;
        }

        isAlive = 7U;
        if (!expectStatus(asharia_scene_world_is_alive(world, &unsupportedEntityRequest, &isAlive),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "is alive with unsupported ABI") ||
            isAlive != 0U) {
            return false;
        }

        isAlive = 7U;
        return expectStatus(asharia_scene_world_is_alive(world, &undersizedEntityRequest, &isAlive),
                            AshariaSceneNativeStatus_UnsupportedAbi,
                            "is alive with undersized request") &&
               isAlive == 0U;
    }

    [[nodiscard]] bool smokeInvalidArgumentsAndAbi() {
        const auto validRequest = worldCreateRequest();
        if (!expectStatus(asharia_scene_world_create(&validRequest, nullptr),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "world create with null output") ||
            !expectStatus(asharia_scene_world_destroy(nullptr),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "world destroy with null handle")) {
            return false;
        }

        AshariaSceneNativeWorld* sentinelWorld = nullptr;
        if (!expectStatus(asharia_scene_world_create(&validRequest, &sentinelWorld),
                          AshariaSceneNativeStatus_Success, "sentinel world create") ||
            sentinelWorld == nullptr) {
            return false;
        }

        bool passed = smokeEntityInvalidArgumentsAndAbi(sentinelWorld);
        AshariaSceneNativeWorld* world = sentinelWorld;
        if (!expectStatus(asharia_scene_world_create(nullptr, &world),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "world create with null request") ||
            world != nullptr) {
            passed = false;
        }

        auto unsupportedRequest = validRequest;
        unsupportedRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION + 1U;
        world = sentinelWorld;
        if (!expectStatus(asharia_scene_world_create(&unsupportedRequest, &world),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "world create with unsupported ABI") ||
            world != nullptr) {
            passed = false;
        }

        auto undersizedRequest = validRequest;
        undersizedRequest.header.structSize =
            static_cast<std::uint32_t>(sizeof(AshariaSceneNativeAbiHeader) - 1U);
        world = sentinelWorld;
        if (!expectStatus(asharia_scene_world_create(&undersizedRequest, &world),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "world create with undersized request") ||
            world != nullptr) {
            passed = false;
        }

        const bool destroyed =
            expectStatus(asharia_scene_world_destroy(sentinelWorld),
                         AshariaSceneNativeStatus_Success, "sentinel world destroy");
        return passed && destroyed;
    }

    [[nodiscard]] bool smokeEntityLifecycle() {
        AshariaSceneNativeWorld* world = nullptr;
        const auto worldRequest = worldCreateRequest();
        if (!expectStatus(asharia_scene_world_create(&worldRequest, &world),
                          AshariaSceneNativeStatus_Success, "entity world create") ||
            world == nullptr) {
            return false;
        }

        const auto createRequest = createEntityRequest();
        AshariaSceneNativeEntityId first{};
        if (!expectStatus(asharia_scene_world_create_entity(world, &createRequest, &first),
                          AshariaSceneNativeStatus_Success, "first entity create") ||
            isZero(first)) {
            (void)asharia_scene_world_destroy(world);
            return false;
        }

        auto firstRequest = entityRequest(first);
        std::uint32_t isAlive = 0U;
        if (!expectStatus(asharia_scene_world_is_alive(world, &firstRequest, &isAlive),
                          AshariaSceneNativeStatus_Success, "first entity is alive") ||
            isAlive != 1U ||
            !expectStatus(asharia_scene_world_destroy_entity(world, &firstRequest),
                          AshariaSceneNativeStatus_Success, "first entity destroy")) {
            (void)asharia_scene_world_destroy(world);
            return false;
        }

        isAlive = 7U;
        if (!expectStatus(asharia_scene_world_is_alive(world, &firstRequest, &isAlive),
                          AshariaSceneNativeStatus_Success, "stale entity is alive") ||
            isAlive != 0U ||
            !expectStatus(asharia_scene_world_destroy_entity(world, &firstRequest),
                          AshariaSceneNativeStatus_InvalidEntity, "stale entity destroy")) {
            (void)asharia_scene_world_destroy(world);
            return false;
        }

        AshariaSceneNativeEntityId second{};
        if (!expectStatus(asharia_scene_world_create_entity(world, &createRequest, &second),
                          AshariaSceneNativeStatus_Success, "second entity create") ||
            second.index != first.index || second.generation == first.generation) {
            (void)asharia_scene_world_destroy(world);
            return false;
        }

        const auto secondRequest = entityRequest(second);
        isAlive = 0U;
        const bool passed =
            expectStatus(asharia_scene_world_is_alive(world, &secondRequest, &isAlive),
                         AshariaSceneNativeStatus_Success, "second entity is alive") &&
            isAlive == 1U &&
            expectStatus(asharia_scene_world_destroy_entity(world, &secondRequest),
                         AshariaSceneNativeStatus_Success, "second entity destroy");
        const bool destroyed =
            expectStatus(asharia_scene_world_destroy(world), AshariaSceneNativeStatus_Success,
                         "entity world destroy");
        return passed && destroyed;
    }

    [[nodiscard]] bool smokeOwnerThreadLifetime() {
        AshariaSceneNativeWorld* world = nullptr;
        const auto request = worldCreateRequest();
        if (!expectStatus(asharia_scene_world_create(&request, &world),
                          AshariaSceneNativeStatus_Success, "world create") ||
            world == nullptr) {
            return false;
        }

        const auto createRequest = createEntityRequest();
        AshariaSceneNativeEntityId entity{};
        if (!expectStatus(asharia_scene_world_create_entity(world, &createRequest, &entity),
                          AshariaSceneNativeStatus_Success,
                          "owner-thread entity create before thread checks")) {
            (void)asharia_scene_world_destroy(world);
            return false;
        }
        const auto existingEntityRequest = entityRequest(entity);

        AshariaSceneNativeEntityId wrongThreadEntity{.index = 7U, .generation = 9U};
        std::uint32_t wrongThreadIsAlive = 7U;
        AshariaSceneNativeStatus wrongThreadCreateStatus = AshariaSceneNativeStatus_InternalError;
        AshariaSceneNativeStatus wrongThreadAliveStatus = AshariaSceneNativeStatus_InternalError;
        AshariaSceneNativeStatus wrongThreadEntityDestroyStatus =
            AshariaSceneNativeStatus_InternalError;
        AshariaSceneNativeStatus wrongThreadWorldDestroyStatus =
            AshariaSceneNativeStatus_InternalError;
        std::thread wrongThread{[&] {
            wrongThreadCreateStatus =
                asharia_scene_world_create_entity(world, &createRequest, &wrongThreadEntity);
            wrongThreadAliveStatus =
                asharia_scene_world_is_alive(world, &existingEntityRequest, &wrongThreadIsAlive);
            wrongThreadEntityDestroyStatus =
                asharia_scene_world_destroy_entity(world, &existingEntityRequest);
            wrongThreadWorldDestroyStatus = asharia_scene_world_destroy(world);
        }};
        wrongThread.join();

        if (!expectStatus(wrongThreadCreateStatus, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread entity create") ||
            !isZero(wrongThreadEntity) ||
            !expectStatus(wrongThreadAliveStatus, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread entity is alive") ||
            wrongThreadIsAlive != 0U ||
            !expectStatus(wrongThreadEntityDestroyStatus, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread entity destroy") ||
            !expectStatus(wrongThreadWorldDestroyStatus, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread world destroy")) {
            (void)asharia_scene_world_destroy(world);
            return false;
        }

        std::uint32_t isAlive = 0U;
        if (!expectStatus(asharia_scene_world_is_alive(world, &existingEntityRequest, &isAlive),
                          AshariaSceneNativeStatus_Success, "owner-thread entity remains alive") ||
            isAlive != 1U ||
            !expectStatus(asharia_scene_world_destroy_entity(world, &existingEntityRequest),
                          AshariaSceneNativeStatus_Success, "owner-thread entity destroy")) {
            (void)asharia_scene_world_destroy(world);
            return false;
        }

        return expectStatus(asharia_scene_world_destroy(world), AshariaSceneNativeStatus_Success,
                            "owner-thread world destroy");
    }

} // namespace

// The exhaustive catch boundary converts all failures to the smoke-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        if (!smokeInvalidArgumentsAndAbi() || !smokeEntityLifecycle() ||
            !smokeOwnerThreadLifetime()) {
            return EXIT_FAILURE;
        }

        std::cout << "Scene native lifecycle smoke tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Scene native lifecycle smoke test threw: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Scene native lifecycle smoke test threw an unknown exception.\n";
    }
    return EXIT_FAILURE;
}
