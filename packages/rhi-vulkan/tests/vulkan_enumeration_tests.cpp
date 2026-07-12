#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vulkan_enumeration.hpp"

namespace {

    struct TestValue {
        std::uint32_t value{};

        constexpr bool operator==(const TestValue&) const = default;
    };

    [[nodiscard]] bool enumeratesZeroValues() {
        std::uint32_t queryCount = 0;
        auto result = asharia::detail::enumerateVulkanVector<TestValue>(
            [&queryCount](std::uint32_t* count, TestValue* values) {
                ++queryCount;
                if (values != nullptr) {
                    return VK_ERROR_UNKNOWN;
                }
                *count = 0;
                return VK_SUCCESS;
            },
            "zero enumeration");

        return result && result->empty() && queryCount == 1;
    }

    [[nodiscard]] bool enumeratesImmediateSuccess() {
        std::uint32_t queryCount = 0;
        auto result = asharia::detail::enumerateVulkanVector<TestValue>(
            [&queryCount](std::uint32_t* count, TestValue* values) {
                ++queryCount;
                if (values == nullptr) {
                    *count = 2;
                    return VK_SUCCESS;
                }
                const std::span output{values, *count};
                output[0] = TestValue{.value = 11};
                output[1] = TestValue{.value = 22};
                *count = 2;
                return VK_SUCCESS;
            },
            "immediate enumeration");

        return result && *result == std::vector<TestValue>{{.value = 11}, {.value = 22}} &&
               queryCount == 2;
    }

    [[nodiscard]] bool retriesIncompleteFillWithFreshCount() {
        std::uint32_t queryCount = 0;
        auto result = asharia::detail::enumerateVulkanVector<TestValue>(
            [&queryCount](std::uint32_t* count, TestValue* values) {
                ++queryCount;
                if (values == nullptr) {
                    *count = queryCount == 1 ? 2U : 3U;
                    return VK_SUCCESS;
                }
                if (queryCount == 2) {
                    const std::span output{values, *count};
                    output[0] = TestValue{.value = 1};
                    output[1] = TestValue{.value = 2};
                    *count = 2;
                    return VK_INCOMPLETE;
                }
                const std::span output{values, *count};
                output[0] = TestValue{.value = 1};
                output[1] = TestValue{.value = 2};
                output[2] = TestValue{.value = 3};
                *count = 3;
                return VK_SUCCESS;
            },
            "growing enumeration");

        return result &&
               *result == std::vector<TestValue>{{.value = 1}, {.value = 2}, {.value = 3}} &&
               queryCount == 4;
    }

    [[nodiscard]] bool shrinksToReturnedCount() {
        auto result = asharia::detail::enumerateVulkanVector<TestValue>(
            [](std::uint32_t* count, TestValue* values) {
                if (values == nullptr) {
                    *count = 3;
                    return VK_SUCCESS;
                }
                const std::span output{values, *count};
                output[0] = TestValue{.value = 7};
                output[1] = TestValue{.value = 8};
                *count = 2;
                return VK_SUCCESS;
            },
            "shrinking enumeration");

        return result && *result == std::vector<TestValue>{{.value = 7}, {.value = 8}};
    }

    [[nodiscard]] bool preservesFailureContextAndVkResult() {
        auto result = asharia::detail::enumerateVulkanVector<TestValue>(
            [](std::uint32_t* count, TestValue* values) {
                if (values == nullptr) {
                    *count = 1;
                    return VK_SUCCESS;
                }
                return VK_ERROR_DEVICE_LOST;
            },
            "device extension enumeration");

        return !result && result.error().code == static_cast<int>(VK_ERROR_DEVICE_LOST) &&
               result.error().message.find("device extension enumeration") != std::string::npos &&
               result.error().message.find("VK_ERROR_DEVICE_LOST") != std::string::npos;
    }

} // namespace

// Unexpected allocation failures are converted to process failure by the runtime.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    using Test = bool (*)();
    const std::array tests{
        std::pair<std::string_view, Test>{"enumeratesZeroValues", enumeratesZeroValues},
        std::pair<std::string_view, Test>{"enumeratesImmediateSuccess", enumeratesImmediateSuccess},
        std::pair<std::string_view, Test>{"retriesIncompleteFillWithFreshCount",
                                          retriesIncompleteFillWithFreshCount},
        std::pair<std::string_view, Test>{"shrinksToReturnedCount", shrinksToReturnedCount},
        std::pair<std::string_view, Test>{"preservesFailureContextAndVkResult",
                                          preservesFailureContextAndVkResult},
    };

    for (const auto& [name, test] : tests) {
        if (!test()) {
            std::cerr << name << " failed.\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << tests.size() << " Vulkan enumeration tests passed.\n";
    return EXIT_SUCCESS;
}
