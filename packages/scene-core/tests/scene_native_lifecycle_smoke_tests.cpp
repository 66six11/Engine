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

    [[nodiscard]] bool expectStatus(AshariaSceneNativeStatus actual,
                                    AshariaSceneNativeStatus expected, std::string_view operation) {
        if (actual == expected) {
            return true;
        }

        std::cerr << operation << " returned status " << actual << ", expected " << expected
                  << ".\n";
        return false;
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

        bool passed = true;
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

    [[nodiscard]] bool smokeOwnerThreadLifetime() {
        AshariaSceneNativeWorld* world = nullptr;
        const auto request = worldCreateRequest();
        if (!expectStatus(asharia_scene_world_create(&request, &world),
                          AshariaSceneNativeStatus_Success, "world create") ||
            world == nullptr) {
            return false;
        }

        AshariaSceneNativeStatus wrongThreadStatus = AshariaSceneNativeStatus_InternalError;
        std::thread wrongThread{[&] { wrongThreadStatus = asharia_scene_world_destroy(world); }};
        wrongThread.join();

        if (!expectStatus(wrongThreadStatus, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread world destroy")) {
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
        if (!smokeInvalidArgumentsAndAbi() || !smokeOwnerThreadLifetime()) {
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
