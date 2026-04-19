#pragma once

#include "vke/core/error.hpp"

#include <expected>

namespace vke {

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

} // namespace vke
