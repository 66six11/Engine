#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "asharia/project/project_descriptor.hpp"
#include "asharia/project/project_descriptor_io.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    [[nodiscard]] bool messageContains(std::string_view message, std::string_view token) {
        return message.find(token) != std::string_view::npos;
    }

    struct InvalidProjectTextCase {
        std::string_view text;
        std::string_view expectedToken;
    };

    [[nodiscard]] bool expectInvalidProjectRead(InvalidProjectTextCase testCase) {
        auto parsed = asharia::project::readAshariaProjectDescriptorText(testCase.text);
        if (parsed) {
            logFailure("Asharia project descriptor smoke accepted invalid input.");
            return false;
        }

        if (!messageContains(parsed.error().message, testCase.expectedToken)) {
            std::cerr << "Asharia project descriptor diagnostic did not contain token \""
                      << testCase.expectedToken << "\": " << parsed.error().message << '\n';
            return false;
        }

        return true;
    }

    [[nodiscard]] bool expectInvalidProject(const asharia::project::AshariaProjectDescriptor& desc,
                                            std::string_view expectedToken) {
        auto valid = asharia::project::validateAshariaProjectDescriptor(desc);
        if (valid) {
            logFailure("Asharia project descriptor smoke accepted invalid descriptor.");
            return false;
        }

        if (!messageContains(valid.error().message, expectedToken)) {
            std::cerr << "Asharia project descriptor diagnostic did not contain token \""
                      << expectedToken << "\": " << valid.error().message << '\n';
            return false;
        }

        return true;
    }

    struct SmokeWorkspace {
        std::filesystem::path root;

        SmokeWorkspace() = default;
        explicit SmokeWorkspace(std::filesystem::path rootPath) : root(std::move(rootPath)) {}

        SmokeWorkspace(const SmokeWorkspace&) = delete;
        SmokeWorkspace& operator=(const SmokeWorkspace&) = delete;

        SmokeWorkspace(SmokeWorkspace&& other) noexcept : root(std::move(other.root)) {}
        SmokeWorkspace& operator=(SmokeWorkspace&& other) noexcept {
            if (this != &other) {
                cleanup();
                root = std::move(other.root);
            }
            return *this;
        }

        ~SmokeWorkspace() {
            cleanup();
        }

        void cleanup() noexcept {
            if (root.empty()) {
                return;
            }

            std::error_code removeError;
            std::filesystem::remove_all(root, removeError);
            root.clear();
        }
    };

    [[nodiscard]] std::optional<SmokeWorkspace> makeSmokeWorkspace() {
        const std::filesystem::path tempRoot = std::filesystem::temp_directory_path();

        for (std::uint64_t attempt = 0; attempt < 32; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const std::filesystem::path candidate =
                tempRoot / ("asharia-project-core-smoke-" + std::to_string(stamp) + "-" +
                            std::to_string(attempt));
            std::error_code createError;
            if (std::filesystem::create_directory(candidate, createError)) {
                return SmokeWorkspace{candidate};
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] asharia::project::AshariaProjectDescriptor makeProjectDescriptor() {
        auto projectId = asharia::project::parseProjectId("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21");
        return asharia::project::AshariaProjectDescriptor{
            .projectName = "SampleProject",
            .projectId = projectId ? *projectId : asharia::project::ProjectId{},
            .assetSourceRoots =
                {
                    asharia::project::AssetSourceRootDesc{
                        .rootName = "project-assets",
                        .directory = "Assets",
                        .sourcePathPrefix = "Assets",
                    },
                },
            .assetCacheRoot = ".asharia/cache/assets",
            .assetDiscovery =
                asharia::project::AssetDiscoveryDesc{
                    .ignoredDirectoryNames = {".git", ".asharia", "Derived"},
                },
        };
    }

    [[nodiscard]] bool smokeProjectId() {
        constexpr std::string_view kLowercaseId = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21";
        auto parsed = asharia::project::parseProjectId(kLowercaseId);
        if (!parsed || !*parsed || asharia::project::formatProjectId(*parsed) != kLowercaseId) {
            logFailure("Asharia project id smoke failed lowercase UUID round-trip.");
            return false;
        }

        constexpr std::string_view kUppercaseId = "9F7A31A0-0B63-4D4C-9F18-BD9A0D2E9C21";
        auto parsedUppercase = asharia::project::parseProjectId(kUppercaseId);
        if (!parsedUppercase ||
            asharia::project::formatProjectId(*parsedUppercase) != kLowercaseId) {
            logFailure("Asharia project id smoke failed uppercase UUID canonicalization.");
            return false;
        }

        if (asharia::project::parseProjectId("00000000-0000-0000-0000-000000000000")) {
            logFailure("Asharia project id smoke accepted a zero UUID.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProjectDescriptorRoundTrip() {
        const asharia::project::AshariaProjectDescriptor descriptor = makeProjectDescriptor();

        auto text = asharia::project::writeAshariaProjectDescriptorText(descriptor);
        if (!text) {
            std::cerr << text.error().message << '\n';
            return false;
        }

        auto parsed = asharia::project::readAshariaProjectDescriptorText(*text);
        if (!parsed || *parsed != descriptor) {
            logFailure("Asharia project descriptor smoke failed text round-trip.");
            return false;
        }

        std::optional<SmokeWorkspace> workspace = makeSmokeWorkspace();
        if (!workspace) {
            logFailure("Asharia project descriptor smoke could not create temp workspace.");
            return false;
        }

        const std::filesystem::path projectPath =
            workspace->root / std::string{asharia::project::kDefaultAshariaProjectFileName};
        auto written = asharia::project::writeAshariaProjectDescriptorFile(projectPath, descriptor);
        if (!written) {
            std::cerr << written.error().message << '\n';
            return false;
        }

        auto parsedFile = asharia::project::readAshariaProjectDescriptorFile(projectPath);
        if (!parsedFile || *parsedFile != descriptor) {
            logFailure("Asharia project descriptor smoke failed file round-trip.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeProjectDescriptorInvalidText() {
        const std::string malformed = "{";
        const std::string wrongSchema = R"json({
  "schema": "com.asharia.other",
  "schemaVersion": 1,
  "projectName": "SampleProject",
  "projectId": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetSourceRoots": [
    {
      "rootName": "project-assets",
      "directory": "Assets",
      "sourcePathPrefix": "Assets"
    }
  ],
  "assetCacheRoot": ".asharia/cache/assets",
  "assetDiscovery": {
    "ignoredDirectories": [".git"]
  }
})json";
        const std::string missingProjectId = R"json({
  "schema": "com.asharia.project",
  "schemaVersion": 1,
  "projectName": "SampleProject",
  "assetSourceRoots": [],
  "assetCacheRoot": ".asharia/cache/assets",
  "assetDiscovery": {
    "ignoredDirectories": []
  }
})json";
        const std::string missingRootName = R"json({
  "schema": "com.asharia.project",
  "schemaVersion": 1,
  "projectName": "SampleProject",
  "projectId": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetSourceRoots": [
    {
      "directory": "Assets",
      "sourcePathPrefix": "Assets"
    }
  ],
  "assetCacheRoot": ".asharia/cache/assets",
  "assetDiscovery": {
    "ignoredDirectories": []
  }
})json";
        const std::string missingIgnoredDirectories = R"json({
  "schema": "com.asharia.project",
  "schemaVersion": 1,
  "projectName": "SampleProject",
  "projectId": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetSourceRoots": [
    {
      "rootName": "project-assets",
      "directory": "Assets",
      "sourcePathPrefix": "Assets"
    }
  ],
  "assetCacheRoot": ".asharia/cache/assets",
  "assetDiscovery": {}
})json";
        const std::string duplicateProjectName = R"json({
  "schema": "com.asharia.project",
  "schemaVersion": 1,
  "projectName": "SampleProject",
  "projectName": "OtherProject",
  "projectId": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetSourceRoots": [
    {
      "rootName": "project-assets",
      "directory": "Assets",
      "sourcePathPrefix": "Assets"
    }
  ],
  "assetCacheRoot": ".asharia/cache/assets",
  "assetDiscovery": {
    "ignoredDirectories": []
  }
})json";
        const std::string invalidRootDirectory = R"json({
  "schema": "com.asharia.project",
  "schemaVersion": 1,
  "projectName": "SampleProject",
  "projectId": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetSourceRoots": [
    {
      "rootName": "project-assets",
      "directory": "../Assets",
      "sourcePathPrefix": "Assets"
    }
  ],
  "assetCacheRoot": ".asharia/cache/assets",
  "assetDiscovery": {
    "ignoredDirectories": []
  }
})json";

        return expectInvalidProjectRead({.text = malformed, .expectedToken = "JSON"}) &&
               expectInvalidProjectRead(
                   {.text = wrongSchema, .expectedToken = "unsupported schema"}) &&
               expectInvalidProjectRead({.text = missingProjectId, .expectedToken = "projectId"}) &&
               expectInvalidProjectRead({.text = missingRootName, .expectedToken = "rootName"}) &&
               expectInvalidProjectRead(
                   {.text = missingIgnoredDirectories, .expectedToken = "ignoredDirectories"}) &&
               expectInvalidProjectRead(
                   {.text = duplicateProjectName, .expectedToken = "duplicate key"}) &&
               expectInvalidProjectRead(
                   {.text = invalidRootDirectory, .expectedToken = "directory"});
    }

    [[nodiscard]] bool smokeProjectDescriptorValidation() {
        asharia::project::AshariaProjectDescriptor descriptor = makeProjectDescriptor();
        asharia::project::AshariaProjectDescriptor invalidProjectId = descriptor;
        invalidProjectId.projectId = {};

        asharia::project::AshariaProjectDescriptor invalidRootName = descriptor;
        invalidRootName.assetSourceRoots.front().rootName = "../bad";

        asharia::project::AshariaProjectDescriptor invalidPrefix = descriptor;
        invalidPrefix.assetSourceRoots.front().sourcePathPrefix = "Assets\\Textures";

        asharia::project::AshariaProjectDescriptor duplicateRoots = descriptor;
        duplicateRoots.assetSourceRoots.push_back(duplicateRoots.assetSourceRoots.front());

        asharia::project::AshariaProjectDescriptor invalidIgnored = descriptor;
        invalidIgnored.assetDiscovery.ignoredDirectoryNames.emplace_back("Bad/Name");

        asharia::project::AshariaProjectDescriptor duplicateIgnored = descriptor;
        duplicateIgnored.assetDiscovery.ignoredDirectoryNames.emplace_back(".git");

        return expectInvalidProject(invalidProjectId, "project id") &&
               expectInvalidProject(invalidRootName, "rootName") &&
               expectInvalidProject(invalidPrefix, "sourcePathPrefix") &&
               expectInvalidProject(duplicateRoots, "duplicate rootName") &&
               expectInvalidProject(invalidIgnored, "ignoredDirectories") &&
               expectInvalidProject(duplicateIgnored, "duplicate name");
    }

} // namespace

// The exhaustive catch boundary converts all failures to the smoke-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        const bool passed = smokeProjectId() && smokeProjectDescriptorRoundTrip() &&
                            smokeProjectDescriptorInvalidText() &&
                            smokeProjectDescriptorValidation();
        if (!passed) {
            return 1;
        }

        std::cout << "Asharia project descriptor smoke passed\n";
        return 0;
    } catch (const std::exception& exception) {
        logFailure(std::string{"Asharia project descriptor smoke threw: "} + exception.what());
        return 1;
    } catch (...) {
        logFailure("Asharia project descriptor smoke caught an unknown exception.");
        return 1;
    }
}
