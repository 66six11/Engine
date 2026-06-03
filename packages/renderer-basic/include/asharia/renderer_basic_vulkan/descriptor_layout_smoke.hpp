#pragma once

#include "asharia/core/result.hpp"
#include "asharia/renderer_basic_vulkan/basic_renderer_descs.hpp"

namespace asharia {

    [[nodiscard]] Result<void>
    validateBasicDescriptorLayoutSmoke(const BasicDescriptorLayoutSmokeDesc& desc);

} // namespace asharia