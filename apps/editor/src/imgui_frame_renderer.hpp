#pragma once

#include "asharia/core/result.hpp"
#include "asharia/rhi_vulkan/vulkan_frame_loop.hpp"

namespace asharia::editor {

    [[nodiscard]] asharia::Result<asharia::VulkanFrameRecordResult>
    recordEditorImguiFrame(const asharia::VulkanFrameRecordContext& context);

} // namespace asharia::editor
