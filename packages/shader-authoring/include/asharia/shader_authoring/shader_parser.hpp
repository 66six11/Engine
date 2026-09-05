#pragma once

#include <string>
#include <string_view>

#include "asharia/shader_authoring/shader_document.hpp"

namespace asharia::shader_authoring {

    struct ShaderParseOptions {
        std::string sourceName;
    };

    ShaderParseResult parseShaderDocument(std::string_view source,
                                            const ShaderParseOptions& options = {});

} // namespace asharia::shader_authoring
