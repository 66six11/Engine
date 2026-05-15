#pragma once

#include <string>
#include <utility>

namespace asharia {

    enum class ErrorDomain {
        Core,
        Platform,
        Vulkan,
        Shader,
        RenderGraph,
        Reflection,
        Serialization,
        Schema,
        Archive,
        CppBinding,
        Persistence,
    };

    struct Error {
        ErrorDomain domain{ErrorDomain::Core};
        int code{0};
        std::string message;

        Error() = default;

        Error(ErrorDomain errorDomain, int errorCode, std::string errorMessage)
            : domain(errorDomain), code(errorCode), message(std::move(errorMessage)) {}
    };

} // namespace asharia
