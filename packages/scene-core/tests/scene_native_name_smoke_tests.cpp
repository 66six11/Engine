#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <thread>
#include <type_traits>

#include "asharia/scene/world_native_api.h"

namespace {

    constexpr std::string_view kUnicodeName{
        "Cube \xE7\xAB\x8B\xE6\x96\xB9\xE4\xBD\x93 \xF0\x9F\xA7\x8A"};

    [[nodiscard]] constexpr std::size_t alignUp(std::size_t value, std::size_t alignment) noexcept {
        return ((value + alignment - 1U) / alignment) * alignment;
    }

    static_assert(sizeof(AshariaSceneNativeCreateEntityRequest) == 8U);
    static_assert(std::is_standard_layout_v<AshariaSceneNativeStringView>);
    static_assert(std::is_trivially_copyable_v<AshariaSceneNativeStringView>);
    static_assert(offsetof(AshariaSceneNativeStringView, data) == 0U);
    static_assert(offsetof(AshariaSceneNativeStringView, byteLength) ==
                  alignUp(sizeof(const char*), alignof(std::uint64_t)));
    static_assert(sizeof(AshariaSceneNativeStringView) ==
                  offsetof(AshariaSceneNativeStringView, byteLength) + sizeof(std::uint64_t));
    static_assert(offsetof(AshariaSceneNativeSetEntityNameRequest, header) == 0U);
    static_assert(offsetof(AshariaSceneNativeSetEntityNameRequest, entity) == 8U);
    static_assert(offsetof(AshariaSceneNativeSetEntityNameRequest, nameUtf8) == 16U);

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

    [[nodiscard]] constexpr AshariaSceneNativeSetEntityNameRequest
    setNameRequest(AshariaSceneNativeEntityId entity, const char* data,
                   std::uint64_t byteLength) noexcept {
        return AshariaSceneNativeSetEntityNameRequest{
            .header = abiHeader(sizeof(AshariaSceneNativeSetEntityNameRequest)),
            .entity = entity,
            .nameUtf8 = {.data = data, .byteLength = byteLength},
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

    [[nodiscard]] bool createWorldAndEntity(AshariaSceneNativeWorld*& world,
                                            AshariaSceneNativeEntityId& entity) {
        world = nullptr;
        entity = {};

        const auto worldRequest = worldCreateRequest();
        if (!expectStatus(asharia_scene_world_create(&worldRequest, &world),
                          AshariaSceneNativeStatus_Success, "name world create") ||
            world == nullptr) {
            return false;
        }

        const auto createRequest = createEntityRequest();
        if (!expectStatus(asharia_scene_world_create_entity(world, &createRequest, &entity),
                          AshariaSceneNativeStatus_Success, "name entity create") ||
            entity.index == 0U || entity.generation == 0U) {
            (void)asharia_scene_world_destroy(world);
            world = nullptr;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool nameEquals(AshariaSceneNativeWorld* world,
                                  const AshariaSceneNativeEntityRequest& request,
                                  std::string_view expected) {
        std::uint64_t required = 99U;
        if (!expectStatus(
                asharia_scene_world_get_entity_name(world, &request, nullptr, 0U, &required),
                AshariaSceneNativeStatus_Success, "query entity name") ||
            required != expected.size()) {
            return false;
        }

        std::array<char, kUnicodeName.size() + 1U> buffer{};
        buffer.fill('?');
        if (required > buffer.size() ||
            !expectStatus(asharia_scene_world_get_entity_name(world, &request, buffer.data(),
                                                              buffer.size(), &required),
                          AshariaSceneNativeStatus_Success, "copy entity name") ||
            std::string_view{buffer.data(), static_cast<std::size_t>(required)} != expected) {
            return false;
        }
        return buffer.at(static_cast<std::size_t>(required)) == '?';
    }

    [[nodiscard]] bool smokeRoundTripAndCopyContract() {
        AshariaSceneNativeWorld* world = nullptr;
        AshariaSceneNativeEntityId entity{};
        if (!createWorldAndEntity(world, entity)) {
            return false;
        }

        const auto getRequest = entityRequest(entity);
        bool passed = nameEquals(world, getRequest, {});

        std::array<char, kUnicodeName.size()> input{};
        std::ranges::copy(kUnicodeName, input.begin());
        const auto setRequest =
            setNameRequest(entity, input.data(), static_cast<std::uint64_t>(input.size()));
        if (!expectStatus(asharia_scene_world_set_entity_name(world, &setRequest),
                          AshariaSceneNativeStatus_Success, "set Unicode entity name")) {
            passed = false;
        }
        input.fill('?');

        if (!nameEquals(world, getRequest, kUnicodeName)) {
            passed = false;
        }

        std::array<char, 4U> tooSmall{'a', 'b', 'c', 'd'};
        const auto before = tooSmall;
        std::uint64_t required = 0U;
        if (!expectStatus(asharia_scene_world_get_entity_name(world, &getRequest, tooSmall.data(),
                                                              tooSmall.size(), &required),
                          AshariaSceneNativeStatus_BufferTooSmall,
                          "copy entity name into undersized buffer") ||
            required != kUnicodeName.size() || tooSmall != before) {
            passed = false;
        }

        const auto clearRequest = setNameRequest(entity, nullptr, 0U);
        if (!expectStatus(asharia_scene_world_set_entity_name(world, &clearRequest),
                          AshariaSceneNativeStatus_Success, "clear entity name") ||
            !nameEquals(world, getRequest, {})) {
            passed = false;
        }

        const bool destroyed =
            expectStatus(asharia_scene_world_destroy(world), AshariaSceneNativeStatus_Success,
                         "name round-trip world destroy");
        return passed && destroyed;
    }

    [[nodiscard]] bool smokeUtf8Validation() {
        AshariaSceneNativeWorld* world = nullptr;
        AshariaSceneNativeEntityId entity{};
        if (!createWorldAndEntity(world, entity)) {
            return false;
        }

        constexpr std::string_view kBaseline{"Baseline"};
        const auto baselineRequest = setNameRequest(entity, kBaseline.data(), kBaseline.size());
        bool passed = expectStatus(asharia_scene_world_set_entity_name(world, &baselineRequest),
                                   AshariaSceneNativeStatus_Success, "set baseline entity name");

        const std::array overlong{static_cast<char>(0xC0U), static_cast<char>(0x80U)};
        const std::array surrogate{static_cast<char>(0xEDU), static_cast<char>(0xA0U),
                                   static_cast<char>(0x80U)};
        const std::array outOfRange{static_cast<char>(0xF4U), static_cast<char>(0x90U),
                                    static_cast<char>(0x80U), static_cast<char>(0x80U)};
        const std::array truncated{static_cast<char>(0xE2U), static_cast<char>(0x82U)};
        const std::array continuation{static_cast<char>(0x80U)};

        struct InvalidCase {
            const char* data;
            std::uint64_t size;
            std::string_view operation;
        };
        const std::array invalidCases{
            InvalidCase{.data = overlong.data(),
                        .size = overlong.size(),
                        .operation = "reject overlong UTF-8"},
            InvalidCase{.data = surrogate.data(),
                        .size = surrogate.size(),
                        .operation = "reject UTF-8 surrogate"},
            InvalidCase{.data = outOfRange.data(),
                        .size = outOfRange.size(),
                        .operation = "reject out-of-range UTF-8"},
            InvalidCase{.data = truncated.data(),
                        .size = truncated.size(),
                        .operation = "reject truncated UTF-8"},
            InvalidCase{.data = continuation.data(),
                        .size = continuation.size(),
                        .operation = "reject isolated continuation byte"},
        };

        for (const InvalidCase& invalid : invalidCases) {
            const auto request = setNameRequest(entity, invalid.data, invalid.size);
            if (!expectStatus(asharia_scene_world_set_entity_name(world, &request),
                              AshariaSceneNativeStatus_InvalidUtf8, invalid.operation)) {
                passed = false;
            }
        }

        const auto invalidPointer = setNameRequest(entity, nullptr, 1U);
        const auto oversized =
            setNameRequest(entity, "x", ASHARIA_SCENE_NATIVE_MAX_ENTITY_NAME_UTF8_BYTES + 1ULL);
        if (!expectStatus(asharia_scene_world_set_entity_name(world, &invalidPointer),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "reject null non-empty UTF-8 input") ||
            !expectStatus(asharia_scene_world_set_entity_name(world, &oversized),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "reject oversized UTF-8 entity name") ||
            !nameEquals(world, entityRequest(entity), kBaseline)) {
            passed = false;
        }

        const bool destroyed =
            expectStatus(asharia_scene_world_destroy(world), AshariaSceneNativeStatus_Success,
                         "UTF-8 validation world destroy");
        return passed && destroyed;
    }

    [[nodiscard]] bool smokeArgumentsAbiThreadAndStaleEntity() {
        AshariaSceneNativeWorld* world = nullptr;
        AshariaSceneNativeEntityId entity{};
        if (!createWorldAndEntity(world, entity)) {
            return false;
        }

        const auto validGetRequest = entityRequest(entity);
        constexpr std::string_view kName{"Thread"};
        const auto validSetRequest = setNameRequest(entity, kName.data(), kName.size());
        bool passed = true;
        std::uint64_t byteLength = 7U;

        if (!expectStatus(asharia_scene_world_get_entity_name(nullptr, &validGetRequest, nullptr,
                                                              0U, &byteLength),
                          AshariaSceneNativeStatus_InvalidArgument, "get name with null world") ||
            byteLength != 0U) {
            passed = false;
        }

        byteLength = 7U;
        if (!expectStatus(
                asharia_scene_world_get_entity_name(world, nullptr, nullptr, 0U, &byteLength),
                AshariaSceneNativeStatus_InvalidArgument, "get name with null request") ||
            byteLength != 0U ||
            !expectStatus(
                asharia_scene_world_get_entity_name(world, &validGetRequest, nullptr, 0U, nullptr),
                AshariaSceneNativeStatus_InvalidArgument, "get name with null length output")) {
            passed = false;
        }

        byteLength = 7U;
        if (!expectStatus(asharia_scene_world_get_entity_name(world, &validGetRequest, nullptr, 1U,
                                                              &byteLength),
                          AshariaSceneNativeStatus_InvalidArgument,
                          "get name with null non-zero-capacity buffer") ||
            byteLength != 0U) {
            passed = false;
        }

        auto unsupportedGetRequest = validGetRequest;
        unsupportedGetRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION + 1U;
        byteLength = 7U;
        if (!expectStatus(asharia_scene_world_get_entity_name(world, &unsupportedGetRequest,
                                                              nullptr, 0U, &byteLength),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "get name with unsupported ABI") ||
            byteLength != 0U) {
            passed = false;
        }

        auto undersizedGetRequest = validGetRequest;
        undersizedGetRequest.header.structSize =
            static_cast<std::uint32_t>(sizeof(AshariaSceneNativeEntityRequest) - 1U);
        byteLength = 7U;
        if (!expectStatus(asharia_scene_world_get_entity_name(world, &undersizedGetRequest, nullptr,
                                                              0U, &byteLength),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "get name with undersized request") ||
            byteLength != 0U) {
            passed = false;
        }

        if (!expectStatus(asharia_scene_world_set_entity_name(nullptr, &validSetRequest),
                          AshariaSceneNativeStatus_InvalidArgument, "set name with null world") ||
            !expectStatus(asharia_scene_world_set_entity_name(world, nullptr),
                          AshariaSceneNativeStatus_InvalidArgument, "set name with null request")) {
            passed = false;
        }

        auto unsupportedSetRequest = validSetRequest;
        unsupportedSetRequest.header.abiVersion = ASHARIA_SCENE_NATIVE_ABI_VERSION + 1U;
        if (!expectStatus(asharia_scene_world_set_entity_name(world, &unsupportedSetRequest),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "set name with unsupported ABI")) {
            passed = false;
        }

        auto undersizedSetRequest = validSetRequest;
        undersizedSetRequest.header.structSize =
            static_cast<std::uint32_t>(sizeof(AshariaSceneNativeSetEntityNameRequest) - 1U);
        if (!expectStatus(asharia_scene_world_set_entity_name(world, &undersizedSetRequest),
                          AshariaSceneNativeStatus_UnsupportedAbi,
                          "set name with undersized request")) {
            passed = false;
        }

        std::array<char, 8U> wrongThreadBuffer{};
        wrongThreadBuffer.fill('?');
        const auto bufferBefore = wrongThreadBuffer;
        AshariaSceneNativeStatus wrongThreadGet = AshariaSceneNativeStatus_InternalError;
        AshariaSceneNativeStatus wrongThreadSet = AshariaSceneNativeStatus_InternalError;
        byteLength = 7U;
        std::thread wrongThread{[&] {
            wrongThreadGet = asharia_scene_world_get_entity_name(
                world, &validGetRequest, wrongThreadBuffer.data(), wrongThreadBuffer.size(),
                &byteLength);
            wrongThreadSet = asharia_scene_world_set_entity_name(world, &validSetRequest);
        }};
        wrongThread.join();
        if (!expectStatus(wrongThreadGet, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread get entity name") ||
            byteLength != 0U || wrongThreadBuffer != bufferBefore ||
            !expectStatus(wrongThreadSet, AshariaSceneNativeStatus_WrongThread,
                          "wrong-thread set entity name")) {
            passed = false;
        }

        if (!expectStatus(asharia_scene_world_destroy_entity(world, &validGetRequest),
                          AshariaSceneNativeStatus_Success,
                          "destroy entity before stale name checks")) {
            passed = false;
        }

        byteLength = 7U;
        if (!expectStatus(asharia_scene_world_get_entity_name(world, &validGetRequest, nullptr, 0U,
                                                              &byteLength),
                          AshariaSceneNativeStatus_InvalidEntity, "get stale entity name") ||
            byteLength != 0U ||
            !expectStatus(asharia_scene_world_set_entity_name(world, &validSetRequest),
                          AshariaSceneNativeStatus_InvalidEntity, "set stale entity name")) {
            passed = false;
        }

        const bool destroyed =
            expectStatus(asharia_scene_world_destroy(world), AshariaSceneNativeStatus_Success,
                         "name validation world destroy");
        return passed && destroyed;
    }

} // namespace

// The exhaustive catch boundary converts all failures to the smoke-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        if (!smokeRoundTripAndCopyContract() || !smokeUtf8Validation() ||
            !smokeArgumentsAbiThreadAndStaleEntity()) {
            return EXIT_FAILURE;
        }

        std::cout << "Scene native UTF-8 entity name smoke tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Scene native UTF-8 entity name smoke test threw: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Scene native UTF-8 entity name smoke test threw an unknown exception.\n";
    }
    return EXIT_FAILURE;
}
