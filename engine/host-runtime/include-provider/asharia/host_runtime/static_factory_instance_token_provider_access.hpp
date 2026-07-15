#pragma once

#include "asharia/host_runtime/static_factory_callbacks.hpp"

namespace asharia::host_runtime {

    class FactoryInstanceTokenProviderAccessV1 final {
    public:
        [[nodiscard]] static FactoryInstanceTokenV1 fromPointer(void* instance) noexcept {
            return FactoryInstanceTokenV1{instance};
        }

        [[nodiscard]] static void* pointer(FactoryInstanceViewV1 instance) noexcept {
            return instance.opaque_;
        }

        [[nodiscard]] static void* consume(FactoryInstanceTokenV1 instance) noexcept {
            void* opaque = instance.opaque_;
            instance.opaque_ = nullptr;
            return opaque;
        }
    };

} // namespace asharia::host_runtime
