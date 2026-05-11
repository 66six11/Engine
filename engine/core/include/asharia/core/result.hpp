#pragma once

#include "asharia/core/error.hpp"

#include <expected>

namespace asharia {

template <typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

} // namespace asharia
