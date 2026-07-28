#pragma once

#include "asharia/host_runtime/static_factory_callbacks.hpp"

namespace asharia::host_runtime::tests {

    [[nodiscard]] StaticFactoryCallbacksV1 abortingCallbacks() noexcept;
    [[nodiscard]] bool runStaticFactoryCallbackTableTests() noexcept;
    [[noreturn]] void runValidTokenDropProbe() noexcept;

} // namespace asharia::host_runtime::tests
