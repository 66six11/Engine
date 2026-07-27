#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <type_traits>

#include "asharia/scene/world_native_api.h"

namespace {

    static_assert(std::is_standard_layout_v<AshariaSceneNativeTransform>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeTransform>);
    static_assert(sizeof(AshariaSceneNativeTransform) == 40U);
    static_assert(offsetof(AshariaSceneNativeTransform, position) == 0U);
    static_assert(offsetof(AshariaSceneNativeTransform, rotation) == 12U);
    static_assert(offsetof(AshariaSceneNativeTransform, scale) == 28U);
    static_assert(sizeof(AshariaSceneNativeSetLocalTransformRequest) == 56U);
    static_assert(offsetof(AshariaSceneNativeSetLocalTransformRequest, transform) == 16U);

    [[nodiscard]] constexpr AshariaSceneNativeAbiHeader abiHeader(std::size_t structSize) noexcept {
        return AshariaSceneNativeAbiHeader{
            .abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION,
            .structSize = static_cast<std::uint32_t>(structSize),
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeWorldCreateRequest worldCreateRequest() noexcept {
        return AshariaSceneNativeWorldCreateRequest{
            .header = abiHeader(sizeof(AshariaSceneNativeWorldCreateRequest)),
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeCreateEntityRequest createEntityRequest() noexcept {
        return AshariaSceneNativeCreateEntityRequest{
            .header = abiHeader(sizeof(AshariaSceneNativeCreateEntityRequest)),
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeEntityRequest
    entityRequest(AshariaSceneNativeEntityId entity) noexcept {
        return AshariaSceneNativeEntityRequest{
            .header = abiHeader(sizeof(AshariaSceneNativeEntityRequest)),
            .entity = entity,
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeTransform identityTransform() noexcept {
        return AshariaSceneNativeTransform{
            .position = {},
            .rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeTransform movedTransform() noexcept {
        constexpr float kHalfSqrtTwo = 0.70710677F;
        return AshariaSceneNativeTransform{
            .position = {.x = 1.5F, .y = -2.25F, .z = 3.75F},
            .rotation = {.x = 0.0F, .y = 0.0F, .z = kHalfSqrtTwo, .w = kHalfSqrtTwo},
            .scale = {.x = 0.0F, .y = -2.0F, .z = 3.0F},
        };
    }

    [[nodiscard]] constexpr AshariaSceneNativeTransform filledTransform(float value) noexcept {
        return AshariaSceneNativeTransform{
            .position = {.x = value, .y = value, .z = value},
            .rotation = {.x = value, .y = value, .z = value, .w = value},
            .scale = {.x = value, .y = value, .z = value},
        };
    }

    [[nodiscard]] constexpr bool equal(AshariaSceneNativeVec3 left,
                                       AshariaSceneNativeVec3 right) noexcept {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

    [[nodiscard]] constexpr bool equal(AshariaSceneNativeQuat left,
                                       AshariaSceneNativeQuat right) noexcept {
        return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
    }

    [[nodiscard]] constexpr bool equal(const AshariaSceneNativeTransform& left,
                                       const AshariaSceneNativeTransform& right) noexcept {
        return equal(left.position, right.position) && equal(left.rotation, right.rotation) &&
               equal(left.scale, right.scale);
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

    [[nodiscard]] bool createWorldAndEntity(AshariaSceneNativeWorld*& world,
                                            AshariaSceneNativeEntityId& entity) {
        world = nullptr;
        entity = {};

        const auto worldRequest = worldCreateRequest();
        if (!expectStatus(asharia_scene_world_create(&worldRequest, &world),
                          AshariaSceneNativeStatus_Success, "transform world create") ||
            world == nullptr) {
            return false;
        }

        const auto createRequest = createEntityRequest();
        if (!expectStatus(asharia_scene_world_create_entity(world, &createRequest, &entity),
                          AshariaSceneNativeStatus_Success, "transform entity create") ||
            entity.index == 0U || entity.generation == 0U) {
            (void)asharia_scene_world_destroy(world);
            world = nullptr;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool smokeDefaultRoundTripAndValidation() {
        AshariaSceneNativeWorld* world = nullptr;
        AshariaSceneNativeEntityId entity{};
        if (!createWorldAndEntity(world, entity)) {
            return false;
        }

        const auto getRequest = entityRequest(entity);
        AshariaSceneNativeTransform actual{};
        bool passed =
            expectStatus(asharia_scene_world_get_local_transform(world, &getRequest, &actual),
                         AshariaSceneNativeStatus_Success, "get default local transform") &&
            equal(actual, identityTransform());

        const AshariaSceneNativeTransform moved = movedTransform();
        AshariaSceneNativeSetLocalTransformRequest setRequest{
            .header = abiHeader(sizeof(AshariaSceneNativeSetLocalTransformRequest)),
            .entity = entity,
            .transform = moved,
        };
        if (!expectStatus(asharia_scene_world_set_local_transform(world, &setRequest),
                          AshariaSceneNativeStatus_Success, "set valid local transform")) {
            passed = false;
        }

        actual = {};
        if (!expectStatus(asharia_scene_world_get_local_transform(world, &getRequest, &actual),
                          AshariaSceneNativeStatus_Success, "get moved local transform") ||
            !equal(actual, moved)) {
            passed = false;
        }

        auto invalid = moved;
        invalid.position.x = std::numeric_limits<float>::quiet_NaN();
        setRequest.transform = invalid;
        if (!expectStatus(asharia_scene_world_set_local_transform(world, &setRequest),
                          AshariaSceneNativeStatus_InvalidTransform,
                          "reject NaN local transform")) {
            passed = false;
        }

        invalid = moved;
        invalid.scale.y = std::numeric_limits<float>::infinity();
        setRequest.transform = invalid;
        if (!expectStatus(asharia_scene_world_set_local_transform(world, &setRequest),
                          AshariaSceneNativeStatus_InvalidTransform,
                          "reject infinite local transform")) {
            passed = false;
        }

        invalid = moved;
        invalid.rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 2.0F};
        setRequest.transform = invalid;
        if (!expectStatus(asharia_scene_world_set_local_transform(world, &setRequest),
                          AshariaSceneNativeStatus_InvalidTransform,
                          "reject non-unit local rotation")) {
            passed = false;
        }

        actual = {};
        if (!expectStatus(asharia_scene_world_get_local_transform(world, &getRequest, &actual),
                          AshariaSceneNativeStatus_Success,
                          "get local transform after rejected updates") ||
            !equal(actual, moved)) {
            passed = false;
        }

        const bool destroyed =
            expectStatus(asharia_scene_world_destroy(world), AshariaSceneNativeStatus_Success,
                         "round-trip world destroy");
        return passed && destroyed;
    }

    [[nodiscard]] bool smokeArgumentsAbiThreadAndStaleEntity() {
        AshariaSceneNativeWorld* world = nullptr;
        AshariaSceneNativeEntityId entity{};
        if (!createWorldAndEntity(world, entity)) {
            return false;
        }

        const auto validGetRequest = entityRequest(entity);
        const AshariaSceneNativeSetLocalTransformRequest validSetRequest{
            .header = abiHeader(sizeof(AshariaSceneNativeSetLocalTransformRequest)),
            .entity = entity,
            .transform = identityTransform(),
        };
        bool passed = true;

        AshariaSceneNativeTransform output = filledTransform(7.0F);
        if (!expectStatus(
                asharia_scene_world_get_local_transform(nullptr, &validGetRequest, &output),
                AshariaSceneNativeStatus_InvalidArgument, "get local transform with null world") ||
            !equal(output, AshariaSceneNativeTransform{})) {
            passed = false;
        }

        output = filledTransform(7.0F);
        if (!expectStatus(asharia_scene_world_get_local_transform(world, nullptr, &output),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "get local transform with null request") ||
            !equal(output, AshariaSceneNativeTransform{}) ||
            !expectStatus(asharia_scene_world_get_local_transform(world, &validGetRequest, nullptr),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "get local transform with null output")) {
            passed = false;
        }

        auto unsupportedGetRequest = validGetRequest;
        unsupportedGetRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION + 1U;
        output = filledTransform(7.0F);
        if (!expectStatus(
                asharia_scene_world_get_local_transform(world, &unsupportedGetRequest, &output),
                AshariaSceneNativeStatus_UnsupportedAbi,
                "get local transform with unsupported ABI") ||
            !equal(output, AshariaSceneNativeTransform{})) {
            passed = false;
        }

        auto undersizedGetRequest = validGetRequest;
        undersizedGetRequest.header.structSize =
            static_cast<std::uint32_t>(sizeof(AshariaSceneNativeEntityRequest) - 1U);
        output = filledTransform(7.0F);
        if (!expectStatus(
                asharia_scene_world_get_local_transform(world, &undersizedGetRequest, &output),
                AshariaSceneNativeStatus_UnsupportedAbi,
                "get local transform with undersized request") ||
            !equal(output, AshariaSceneNativeTransform{})) {
            passed = false;
        }

        if (!expectStatus(asharia_scene_world_set_local_transform(nullptr, &validSetRequest),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "set local transform with null world") ||
            !expectStatus(asharia_scene_world_set_local_transform(world, nullptr),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "set local transform with null request")) {
            passed = false;
        }

        auto unsupportedSetRequest = validSetRequest;
        unsupportedSetRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION + 1U;
        if (!expectStatus(asharia_scene_world_set_local_transform(world, &unsupportedSetRequest),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "set local transform with unsupported ABI")) {
            passed = false;
        }

        auto undersizedSetRequest = validSetRequest;
        undersizedSetRequest.header.structSize =
            static_cast<std::uint32_t>(sizeof(AshariaSceneNativeSetLocalTransformRequest) - 1U);
        if (!expectStatus(asharia_scene_world_set_local_transform(world, &undersizedSetRequest),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "set local transform with undersized request")) {
            passed = false;
        }

        output = filledTransform(7.0F);
        AshariaSceneNativeStatus wrongThreadGet = AshariaSceneNativeStatus_InternalError;
        AshariaSceneNativeStatus wrongThreadSet = AshariaSceneNativeStatus_InternalError;
        std::thread wrongThread{[&] {
            wrongThreadGet =
                asharia_scene_world_get_local_transform(world, &validGetRequest, &output);
            wrongThreadSet = asharia_scene_world_set_local_transform(world, &validSetRequest);
        }};
        wrongThread.join();
        if (!expectStatus(wrongThreadGet, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread get local transform") ||
            !equal(output, AshariaSceneNativeTransform{}) ||
            !expectStatus(wrongThreadSet, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread set local transform")) {
            passed = false;
        }

        if (!expectStatus(asharia_scene_world_destroy_entity(world, &validGetRequest),
                          AshariaSceneNativeStatus_Success,
                          "destroy entity before stale transform checks")) {
            passed = false;
        }

        output = filledTransform(7.0F);
        if (!expectStatus(asharia_scene_world_get_local_transform(world, &validGetRequest, &output),
                          AshariaSceneNativeStatus_InvalidEntity,
                          "get stale entity local transform") ||
            !equal(output, AshariaSceneNativeTransform{}) ||
            !expectStatus(asharia_scene_world_set_local_transform(world, &validSetRequest),
                          AshariaSceneNativeStatus_InvalidEntity,
                          "set stale entity local transform")) {
            passed = false;
        }

        const bool destroyed =
            expectStatus(asharia_scene_world_destroy(world), AshariaSceneNativeStatus_Success,
                         "validation world destroy");
        return passed && destroyed;
    }

} // namespace

// The exhaustive catch boundary converts all failures to the smoke-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        if (!smokeDefaultRoundTripAndValidation() || !smokeArgumentsAbiThreadAndStaleEntity()) {
            return EXIT_FAILURE;
        }

        std::cout << "Scene native local Transform smoke tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Scene native local Transform smoke test threw: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Scene native local Transform smoke test threw an unknown exception.\n";
    }
    return EXIT_FAILURE;
}
