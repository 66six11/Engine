#pragma once

namespace asharia {

    struct VulkanDeviceCapabilities {
        // Capabilities describe features enabled on the logical device, not only physical support.
        bool fillModeNonSolid{};
    };

} // namespace asharia
