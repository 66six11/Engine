#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "asset_processor_dry_run.hpp"
#include "asset_processor_smoke.hpp"

namespace asharia::asset_processor {
    namespace {

        enum class CommandKind : std::uint8_t {
            Help,
            DryRun,
            SmokeDryRun,
        };

        enum class DryRunOptionKind : std::uint8_t {
            Project,
            SourceRoot,
            SourcePathPrefix,
            TargetProfile,
            ProductManifest,
            IgnoreDirectory,
            Unknown,
        };

        struct ParsedArguments {
            CommandKind command{CommandKind::Help};
            DryRunOptions dryRun;
            std::string error;
        };

        struct ArgumentValueResult {
            bool succeeded{};
            std::string value;
            std::string error;
        };

        [[nodiscard]] ArgumentValueResult readArgumentValue(std::span<char* const> arguments,
                                                            std::size_t& index,
                                                            std::string_view optionName) {
            if (index + 1 >= arguments.size()) {
                return ArgumentValueResult{
                    .succeeded = false,
                    .value = {},
                    .error = "Missing value for option " + std::string{optionName} + ".",
                };
            }

            ++index;
            return ArgumentValueResult{
                .succeeded = true,
                .value = arguments[index],
                .error = {},
            };
        }

        [[nodiscard]] DryRunOptionKind classifyDryRunOption(std::string_view option) noexcept {
            if (option == "--project") {
                return DryRunOptionKind::Project;
            }
            if (option == "--source-root") {
                return DryRunOptionKind::SourceRoot;
            }
            if (option == "--source-path-prefix") {
                return DryRunOptionKind::SourcePathPrefix;
            }
            if (option == "--target-profile") {
                return DryRunOptionKind::TargetProfile;
            }
            if (option == "--product-manifest") {
                return DryRunOptionKind::ProductManifest;
            }
            if (option == "--ignore-dir") {
                return DryRunOptionKind::IgnoreDirectory;
            }
            return DryRunOptionKind::Unknown;
        }

        [[nodiscard]] bool applyDryRunOption(DryRunOptions& options, DryRunOptionKind option,
                                             std::string value) {
            switch (option) {
            case DryRunOptionKind::Project:
                if (!value.empty()) {
                    options.projectPath = std::filesystem::path{value};
                }
                return true;
            case DryRunOptionKind::SourceRoot:
                options.sourceRoot = std::filesystem::path{value};
                return true;
            case DryRunOptionKind::SourcePathPrefix:
                options.sourcePathPrefix = std::move(value);
                return true;
            case DryRunOptionKind::TargetProfile:
                options.targetProfile = std::move(value);
                return true;
            case DryRunOptionKind::ProductManifest:
                if (!value.empty()) {
                    options.productManifestPath = std::filesystem::path{value};
                }
                return true;
            case DryRunOptionKind::IgnoreDirectory:
                options.ignoredDirectoryNames.push_back(std::move(value));
                return true;
            case DryRunOptionKind::Unknown:
                return false;
            }
            return false;
        }

        [[nodiscard]] ParsedArguments parseDryRunArguments(std::span<char* const> arguments) {
            ParsedArguments parsed{
                .command = CommandKind::DryRun,
                .dryRun = {},
                .error = {},
            };

            for (std::size_t index = 2; index < arguments.size(); ++index) {
                const std::string_view option = arguments[index];
                const DryRunOptionKind optionKind = classifyDryRunOption(option);
                if (optionKind == DryRunOptionKind::Unknown) {
                    parsed.error = "Unknown dry-run option " + std::string{option} + ".";
                    return parsed;
                }

                ArgumentValueResult value = readArgumentValue(arguments, index, option);
                if (!value.succeeded) {
                    parsed.error = std::move(value.error);
                    return parsed;
                }

                if (!applyDryRunOption(parsed.dryRun, optionKind, std::move(value.value))) {
                    parsed.error = "Unknown dry-run option " + std::string{option} + ".";
                    return parsed;
                }
            }

            if (parsed.dryRun.projectPath &&
                (!parsed.dryRun.sourceRoot.empty() || !parsed.dryRun.sourcePathPrefix.empty())) {
                parsed.error = "dry-run --project cannot be combined with --source-root or "
                               "--source-path-prefix.";
            } else if (!parsed.dryRun.projectPath && parsed.dryRun.sourceRoot.empty()) {
                parsed.error = "dry-run requires --source-root.";
            } else if (!parsed.dryRun.projectPath && parsed.dryRun.sourcePathPrefix.empty()) {
                parsed.error = "dry-run requires --source-path-prefix.";
            } else if (parsed.dryRun.targetProfile.empty()) {
                parsed.error = "dry-run requires --target-profile.";
            }

            return parsed;
        }

        [[nodiscard]] ParsedArguments parseArguments(std::span<char* const> arguments) {
            if (arguments.size() <= 1) {
                return ParsedArguments{.command = CommandKind::Help, .dryRun = {}, .error = {}};
            }

            const std::string_view command = arguments[1];
            if (command == "--help" || command == "-h" || command == "help") {
                return ParsedArguments{.command = CommandKind::Help, .dryRun = {}, .error = {}};
            }

            if (command == "--smoke-dry-run") {
                return ParsedArguments{
                    .command = CommandKind::SmokeDryRun,
                    .dryRun = {},
                    .error = {},
                };
            }

            if (command == "dry-run") {
                return parseDryRunArguments(arguments);
            }

            return ParsedArguments{
                .command = CommandKind::Help,
                .dryRun = {},
                .error = "Unknown command " + std::string{command} + ".",
            };
        }

        void printUsage(std::ostream& output) {
            output << "Usage:\n"
                   << "  asharia-asset-processor dry-run --source-root <path> "
                      "--source-path-prefix <path> --target-profile <name> "
                      "[--product-manifest <path>] [--ignore-dir <name> ...]\n"
                   << "  asharia-asset-processor dry-run --project <path> "
                      "--target-profile <name> [--product-manifest <path>] "
                      "[--ignore-dir <name> ...]\n"
                   << "  asharia-asset-processor --smoke-dry-run\n";
        }

        [[nodiscard]] int runMain(std::span<char* const> arguments) {
            const ParsedArguments parsed = parseArguments(arguments);
            if (!parsed.error.empty()) {
                std::cerr << parsed.error << '\n';
                printUsage(std::cerr);
                return EXIT_FAILURE;
            }

            switch (parsed.command) {
            case CommandKind::Help:
                printUsage(std::cout);
                return EXIT_SUCCESS;
            case CommandKind::DryRun: {
                const DryRunExecution execution = runDryRun(parsed.dryRun);
                std::cout << execution.text;
                return execution.exitCode;
            }
            case CommandKind::SmokeDryRun:
                return runSmokeDryRun();
            }

            return EXIT_FAILURE;
        }

    } // namespace
} // namespace asharia::asset_processor

// main catches all exceptions and reports them as process failure diagnostics.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argumentCount, char** argumentValues) noexcept {
    try {
        return asharia::asset_processor::runMain(
            std::span<char* const>{argumentValues, static_cast<std::size_t>(argumentCount)});
    } catch (const std::exception& exception) {
        std::cerr << "asset-processor failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "asset-processor failed with an unknown exception.\n";
        return EXIT_FAILURE;
    }
}
