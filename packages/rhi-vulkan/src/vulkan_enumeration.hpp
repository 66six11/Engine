#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_error.hpp"

namespace asharia::detail {

    template <typename ValueT, typename QueryT>
    [[nodiscard]] Result<std::vector<ValueT>>
    enumerateVulkanVector(QueryT&& query, std::string_view failureContext) {
        while (true) {
            std::uint32_t count = 0;
            VkResult result = query(&count, nullptr);
            if (result == VK_INCOMPLETE) {
                continue;
            }
            if (result != VK_SUCCESS) {
                return std::unexpected{vulkanError(std::string{failureContext}, result)};
            }

            std::vector<ValueT> values(count);
            if (count == 0) {
                return values;
            }

            result = query(&count, values.data());
            if (result == VK_SUCCESS) {
                values.resize(count);
                return values;
            }
            if (result != VK_INCOMPLETE) {
                return std::unexpected{vulkanError(std::string{failureContext}, result)};
            }
        }
    }

} // namespace asharia::detail
