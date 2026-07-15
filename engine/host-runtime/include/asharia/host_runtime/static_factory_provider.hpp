#pragma once

namespace asharia::host_runtime {

    class StaticFactoryRegistrar;

    using StaticFactoryProviderV1 = void (*)(StaticFactoryRegistrar& registrar) noexcept;

} // namespace asharia::host_runtime
