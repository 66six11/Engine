#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/log.hpp"
#include "asharia/core/version.hpp"

#include "editor_app.hpp"
#include "editor_asset_catalog.hpp"
#include "editor_asset_catalog_report.hpp"
#include "native_bridge/frame_debugger_native_smoke.hpp"
#include "native_bridge/viewport_native_smoke.hpp"

namespace {

    [[nodiscard]] bool argEquals(const char* arg, std::string_view expected) {
        return arg != nullptr && std::string_view{arg} == expected;
    }

    [[nodiscard]] bool hasArg(std::span<char*> args, std::string_view expected) {
        return std::ranges::any_of(
            args, [expected](const char* arg) { return argEquals(arg, expected); });
    }

    struct OptionValueResult {
        bool succeeded{false};
        std::string value;
        std::string error;
    };

    [[nodiscard]] OptionValueResult readOptionValue(std::span<char*> args, std::size_t& index,
                                                    std::string_view option) {
        const std::size_t valueIndex = index + 1U;
        if (valueIndex >= args.size() || args[valueIndex] == nullptr ||
            std::string_view{args[valueIndex]}.starts_with("--")) {
            return OptionValueResult{.succeeded = false,
                                     .value = {},
                                     .error = "Missing value for " + std::string{option} + "."};
        }
        index = valueIndex;
        return OptionValueResult{
            .succeeded = true,
            .value = args[valueIndex],
            .error = {},
        };
    }

    struct ParsedInteractiveArgs {
        asharia::editor::EditorAssetCatalogRunConfig assetCatalog;
        bool checkProject{false};
        bool checkProjectJson{false};
        std::string error;
    };

    enum class InteractiveOption : std::uint8_t {
        Project,
        CheckProject,
        CheckProjectJson,
        Json,
        ProductManifest,
        AssetTargetProfile,
        Unknown,
    };

    [[nodiscard]] InteractiveOption interactiveOptionFor(std::string_view option) noexcept {
        if (option == "--project") {
            return InteractiveOption::Project;
        }
        if (option == "--check-project") {
            return InteractiveOption::CheckProject;
        }
        if (option == "--check-project-json") {
            return InteractiveOption::CheckProjectJson;
        }
        if (option == "--json") {
            return InteractiveOption::Json;
        }
        if (option == "--product-manifest") {
            return InteractiveOption::ProductManifest;
        }
        if (option == "--asset-target-profile") {
            return InteractiveOption::AssetTargetProfile;
        }
        return InteractiveOption::Unknown;
    }

    [[nodiscard]] std::optional<std::string> readParsedOptionValue(std::span<char*> args,
                                                                   std::size_t& index,
                                                                   std::string_view option,
                                                                   ParsedInteractiveArgs& parsed) {
        OptionValueResult result = readOptionValue(args, index, option);
        if (!result.succeeded) {
            parsed.error = std::move(result.error);
            return std::nullopt;
        }
        return std::move(result.value);
    }

    [[nodiscard]] bool parseInteractiveOption(std::span<char*> args, std::size_t& index,
                                              ParsedInteractiveArgs& parsed) {
        const std::string_view option{args[index]};
        switch (interactiveOptionFor(option)) {
        case InteractiveOption::Project:
            if (std::optional<std::string> value =
                    readParsedOptionValue(args, index, option, parsed)) {
                parsed.assetCatalog.projectFile = std::filesystem::path{*value};
            }
            return parsed.error.empty();
        case InteractiveOption::CheckProject:
            if (std::optional<std::string> value =
                    readParsedOptionValue(args, index, option, parsed)) {
                parsed.checkProject = true;
                parsed.assetCatalog.projectFile = std::filesystem::path{*value};
            }
            return parsed.error.empty();
        case InteractiveOption::CheckProjectJson:
            if (std::optional<std::string> value =
                    readParsedOptionValue(args, index, option, parsed)) {
                parsed.checkProject = true;
                parsed.checkProjectJson = true;
                parsed.assetCatalog.projectFile = std::filesystem::path{*value};
            }
            return parsed.error.empty();
        case InteractiveOption::Json:
            parsed.checkProjectJson = true;
            return true;
        case InteractiveOption::ProductManifest:
            if (std::optional<std::string> value =
                    readParsedOptionValue(args, index, option, parsed)) {
                parsed.assetCatalog.productManifestFile = std::filesystem::path{*value};
            }
            return parsed.error.empty();
        case InteractiveOption::AssetTargetProfile:
            if (std::optional<std::string> value =
                    readParsedOptionValue(args, index, option, parsed)) {
                parsed.assetCatalog.targetProfile = std::move(*value);
            }
            return parsed.error.empty();
        case InteractiveOption::Unknown:
            parsed.error = "Unknown editor option " + std::string{option} + ".";
            return false;
        }
        parsed.error = "Unknown editor option " + std::string{option} + ".";
        return false;
    }

    void validateInteractiveArgs(ParsedInteractiveArgs& parsed) {
        if (parsed.assetCatalog.projectFile.empty() &&
            (!parsed.assetCatalog.productManifestFile.empty() ||
             !parsed.assetCatalog.targetProfile.empty())) {
            parsed.error = "--product-manifest and --asset-target-profile require --project.";
        }
        if (parsed.checkProjectJson && !parsed.checkProject) {
            parsed.error = "--json requires --check-project.";
        }
    }

    [[nodiscard]] ParsedInteractiveArgs parseInteractiveArgs(std::span<char*> args) {
        ParsedInteractiveArgs parsed{};
        for (std::size_t index = 1U; index < args.size(); ++index) {
            if (args[index] != nullptr && !parseInteractiveOption(args, index, parsed)) {
                return parsed;
            }
        }
        validateInteractiveArgs(parsed);
        return parsed;
    }

    void printUsage();

    [[nodiscard]] bool validateSingleSmokeArg(std::span<char*> args, std::string_view option) {
        if (args.size() == 2U) {
            return true;
        }
        asharia::logError("Editor smoke option " + std::string{option} +
                          " cannot be combined with project-loading options.");
        printUsage();
        return false;
    }

    [[nodiscard]] asharia::editor::EditorAssetCatalogSnapshotRequest
    snapshotRequestFor(const asharia::editor::EditorAssetCatalogRunConfig& config) {
        std::string targetProfile = config.targetProfile;
        if (targetProfile.empty()) {
            targetProfile = "editor-preview";
        }
        return asharia::editor::EditorAssetCatalogSnapshotRequest{
            .projectFile = config.projectFile,
            .productManifestFile = config.productManifestFile,
            .targetProfile = std::move(targetProfile),
        };
    }

    [[nodiscard]] int
    checkProjectAssetCatalog(const asharia::editor::EditorAssetCatalogRunConfig& config,
                             bool jsonOutput) {
        const asharia::editor::EditorAssetCatalogSnapshotRequest request =
            snapshotRequestFor(config);
        const asharia::editor::EditorAssetCatalogSnapshot snapshot =
            asharia::editor::loadEditorAssetCatalogSnapshot(request);

        if (jsonOutput) {
            auto report =
                asharia::editor::writeEditorAssetCatalogSnapshotJsonReport(request, snapshot);
            if (!report) {
                asharia::logError(report.error().message);
                return EXIT_FAILURE;
            }
            std::cout << *report;
        } else {
            std::cout << asharia::editor::writeEditorAssetCatalogSnapshotTextReport(request,
                                                                                    snapshot);
        }
        return snapshot.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    [[nodiscard]] int runNativeBridgeSmoke(std::span<char*> args) {
        if (!validateSingleSmokeArg(args, "--smoke-editor-native-bridge")) {
            return EXIT_FAILURE;
        }
        if (!asharia::editor::runFrameDebuggerNativeBridgeSmoke()) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    [[nodiscard]] int runViewportNativeSmoke(std::span<char*> args) {
        if (!validateSingleSmokeArg(args, "--smoke-editor-viewport-native")) {
            return EXIT_FAILURE;
        }
        if (!asharia::editor::runViewportNativeBridgeSmoke()) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    void printVersion() {
        std::cout << asharia::kEngineName << " editor " << asharia::kEngineVersion.major << '.'
                  << asharia::kEngineVersion.minor << '.' << asharia::kEngineVersion.patch << '\n';
    }

    void printUsage() {
        std::cout
            << "Usage: asharia-editor [--help] [--version] [--smoke-editor-shell] "
               "[--smoke-editor-asset-browser] [--smoke-editor-viewport] "
               "[--smoke-editor-viewport-resize] [--smoke-editor-frame-debugger] "
               "[--smoke-editor-native-bridge] [--smoke-editor-viewport-native]\n"
               "       asharia-editor [--project <asharia.project.json|project-dir>] "
               "[--product-manifest <products.aproducts.json>] "
               "[--asset-target-profile <profile>]\n"
               "       asharia-editor --check-project <asharia.project.json|project-dir> "
               "[--json] [--product-manifest <products.aproducts.json>] "
               "[--asset-target-profile <profile>]\n"
               "       asharia-editor --check-project-json <asharia.project.json|project-dir> "
               "[--product-manifest <products.aproducts.json>] "
               "[--asset-target-profile <profile>]\n";
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
            if (!validateSingleSmokeArg(args, "--smoke-editor-shell")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeShell);
        }

        if (hasArg(args, "--smoke-editor-asset-browser")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-asset-browser")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeAssetBrowser);
        }

        if (hasArg(args, "--smoke-editor-viewport")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-viewport")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeViewport);
        }

        if (hasArg(args, "--smoke-editor-viewport-resize")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-viewport-resize")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeViewportResize);
        }

        if (hasArg(args, "--smoke-editor-frame-debugger")) {
            if (!validateSingleSmokeArg(args, "--smoke-editor-frame-debugger")) {
                return EXIT_FAILURE;
            }
            return asharia::editor::runEditor(asharia::editor::EditorRunMode::SmokeFrameDebugger);
        }

        if (hasArg(args, "--smoke-editor-native-bridge")) {
            return runNativeBridgeSmoke(args);
        }

        if (hasArg(args, "--smoke-editor-viewport-native")) {
            return runViewportNativeSmoke(args);
        }

        if (args.size() == 1) {
            return asharia::editor::runEditor(asharia::editor::EditorRunConfig{
                .mode = asharia::editor::EditorRunMode::Interactive, .assetCatalog = {}});
        }

        const ParsedInteractiveArgs parsed = parseInteractiveArgs(args);
        if (!parsed.error.empty()) {
            asharia::logError(parsed.error);
            printUsage();
            return EXIT_FAILURE;
        }
        if (parsed.checkProject) {
            return checkProjectAssetCatalog(parsed.assetCatalog, parsed.checkProjectJson);
        }

        return asharia::editor::runEditor(
            asharia::editor::EditorRunConfig{.mode = asharia::editor::EditorRunMode::Interactive,
                                             .assetCatalog = parsed.assetCatalog});
    } catch (const std::exception& exception) {
        asharia::logError(exception.what());
    } catch (...) {
        asharia::logError("Unhandled non-standard exception.");
    }

    return EXIT_FAILURE;
}
