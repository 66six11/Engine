#pragma once

#include <string>
#include <string_view>

#include "asharia/shader_authoring/ashader_document.hpp"

namespace asharia::shader_authoring {

    struct AshaderParseOptions {
        std::string sourceName;
    };

    AshaderParseResult parseAshaderDocument(std::string_view source,
                                            const AshaderParseOptions& options = {});

} // namespace asharia::shader_authoring
