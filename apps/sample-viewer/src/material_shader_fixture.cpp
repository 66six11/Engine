#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>

#include "asharia/core/file_io.hpp"
#include "asharia/shader_authoring/ashader_generated_slang.hpp"
#include "asharia/shader_authoring/ashader_parser.hpp"

// Build-only fixture adapter: exercise the production parser/emitter before slangc/spirv-val.
int main(int argc, char** argv) try {
    if (argc != 3) {
        return EXIT_FAILURE;
    }
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    auto text = asharia::core::readFileText(args[1], {.maxBytes = 65536});
    if (!text) {
        std::cerr << text.error().message;
        return EXIT_FAILURE;
    }
    auto parsed = asharia::shader_authoring::parseAshaderDocument(*text);
    if (!parsed.document || asharia::shader_authoring::hasErrors(parsed.diagnostics)) {
        std::cerr << "Invalid material shader fixture";
        return EXIT_FAILURE;
    }
    asharia::shader_authoring::GeneratedSlangOptions options;
    options.materialSet = 1;
    options.sourceName = args[1];
    auto generated = asharia::shader_authoring::buildGeneratedSlang(*parsed.document, options);
    if (asharia::shader_authoring::hasErrors(generated.diagnostics)) {
        return EXIT_FAILURE;
    }
    auto written = asharia::core::writeFileTextAtomically(args[2], generated.source);
    if (!written) {
        std::cerr << written.error().message;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
} catch (const std::exception& error) {
    std::fputs(error.what(), stderr);
    return EXIT_FAILURE;
} catch (...) {
    return EXIT_FAILURE;
}
