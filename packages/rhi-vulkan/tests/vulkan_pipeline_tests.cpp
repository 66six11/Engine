#include <vulkan/vulkan.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/error.hpp"
#include "asharia/rhi_vulkan/vulkan_pipeline.hpp"

namespace {

    [[nodiscard]] bool defaultsToSolidFill() {
        const asharia::VulkanGraphicsPipelineDesc desc{};
        return desc.polygonMode == VK_POLYGON_MODE_FILL;
    }

    [[nodiscard]] bool rejectsLineModeWithoutEnabledCapability() {
        auto result = asharia::VulkanGraphicsPipeline::createDynamicRendering(
            asharia::VulkanGraphicsPipelineDesc{
                .vertexBindings = {},
                .vertexAttributes = {},
                .polygonMode = VK_POLYGON_MODE_LINE,
                .deviceCapabilities = {},
            });

        return !result && result.error().domain == asharia::ErrorDomain::Vulkan &&
               result.error().code == static_cast<int>(VK_ERROR_FEATURE_NOT_PRESENT) &&
               result.error().message.find("fillModeNonSolid") != std::string::npos;
    }

    [[nodiscard]] bool acceptsLineModeCapabilityBeforeInputValidation() {
        auto result = asharia::VulkanGraphicsPipeline::createDynamicRendering(
            asharia::VulkanGraphicsPipelineDesc{
                .vertexBindings = {},
                .vertexAttributes = {},
                .polygonMode = VK_POLYGON_MODE_LINE,
                .deviceCapabilities =
                    asharia::VulkanDeviceCapabilities{
                        .fillModeNonSolid = true,
                    },
            });

        return !result && result.error().domain == asharia::ErrorDomain::Vulkan &&
               result.error().code != static_cast<int>(VK_ERROR_FEATURE_NOT_PRESENT) &&
               result.error().message.find("incomplete inputs") != std::string::npos;
    }

} // namespace

// Unexpected allocation failures are converted to process failure by the runtime.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    using Test = bool (*)();
    const std::array tests{
        std::pair<std::string_view, Test>{"defaultsToSolidFill", defaultsToSolidFill},
        std::pair<std::string_view, Test>{"rejectsLineModeWithoutEnabledCapability",
                                          rejectsLineModeWithoutEnabledCapability},
        std::pair<std::string_view, Test>{"acceptsLineModeCapabilityBeforeInputValidation",
                                          acceptsLineModeCapabilityBeforeInputValidation},
    };

    for (const auto& [name, test] : tests) {
        if (!test()) {
            std::cerr << name << " failed.\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << tests.size() << " Vulkan pipeline tests passed.\n";
    return EXIT_SUCCESS;
}
