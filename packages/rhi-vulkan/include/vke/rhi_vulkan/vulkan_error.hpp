#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <utility>

#include "vke/core/error.hpp"

namespace vke {

    [[nodiscard]] std::string vkResultName(VkResult result);
    [[nodiscard]] std::string vulkanVersionString(std::uint32_t version);

    [[nodiscard]] inline Error vulkanError(std::string message,
                                           VkResult result = VK_ERROR_UNKNOWN) {
        if (result != VK_SUCCESS) {
            message += ": ";
            message += vkResultName(result);
        }

        return Error{ErrorDomain::Vulkan, static_cast<int>(result), std::move(message)};
    }

} // namespace vke
