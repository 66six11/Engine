#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <string_view>

#include "asharia/core/log.hpp"
#include "asharia/core/version.hpp"

#include "editor_app.hpp"

namespace {

    bool hasArg(std::span<char*> args, std::string_view expected) {
        return std::ranges::any_of(
            args, [expected](const char* arg) { return arg != nullptr && arg == expected; });
    }

    void printVersion() {
        std::cout << asharia::kEngineName << " editor " << asharia::kEngineVersion.major << '.'
                  << asharia::kEngineVersion.minor << '.' << asharia::kEngineVersion.patch << '\n';
    }

    void printUsage() {
        std::cout << "Usage: asharia-editor [--help] [--version] [--smoke-editor-shell] "
                     "[--smoke-editor-viewport] [--smoke-editor-viewport-resize]\n";
    }

} // namespace

int main(int argc, char** argv) {
    try {
        std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        if (hasArg(args, "--help")) {
            printUsage();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--version")) {
            printVersion();
            return EXIT_SUCCESS;
        }

        if (hasArg(args, "--smoke-editor-shell")) {
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeShell);
        }

        if (hasArg(args, "--smoke-editor-viewport")) {
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeViewport);
        }

        if (hasArg(args, "--smoke-editor-viewport-resize")) {
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeViewportResize);
        }

        if (args.size() == 1) {
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::Interactive);
        }

        printVersion();
        printUsage();
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        asharia::logError(exception.what());
    } catch (...) {
        asharia::logError("Unhandled non-standard exception.");
    }

    return EXIT_FAILURE;
}
