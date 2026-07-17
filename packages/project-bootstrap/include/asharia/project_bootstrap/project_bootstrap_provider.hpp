#pragma once

#include "asharia/host_runtime/static_factory_provider.hpp"

namespace asharia::project_bootstrap {

    void provideProjectBootstrapFactories(host_runtime::StaticFactoryRegistrar& registrar) noexcept;

} // namespace asharia::project_bootstrap
