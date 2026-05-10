#pragma once

#include <string>
#include <utility>

namespace vke {

enum class ErrorDomain {
    Core,
    Platform,
    Vulkan,
    Shader,
    RenderGraph,
    Reflection,
    Serialization,
};

struct Error {
    ErrorDomain domain{ErrorDomain::Core};
    int code{0};
    std::string message;

    Error() = default;

    Error(ErrorDomain errorDomain, int errorCode, std::string errorMessage)
        : domain(errorDomain)
        , code(errorCode)
        , message(std::move(errorMessage)) {}
};

} // namespace vke
