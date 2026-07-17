#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "asharia/host_runtime/process_application.hpp"
#include "asharia/project/project_descriptor.hpp"
#include "asharia/project/project_descriptor_io.hpp"
#include "asharia/project_bootstrap/project_bootstrap_reader.hpp"

namespace {

    struct TestWorkspace final {
        std::filesystem::path root;

        TestWorkspace() = default;
        explicit TestWorkspace(std::filesystem::path path) : root(std::move(path)) {}

        TestWorkspace(const TestWorkspace&) = delete;
        TestWorkspace& operator=(const TestWorkspace&) = delete;
        TestWorkspace(TestWorkspace&& other) noexcept : root(std::move(other.root)) {}
        TestWorkspace& operator=(TestWorkspace&&) = delete;

        ~TestWorkspace() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    };

    [[nodiscard]] std::optional<TestWorkspace> makeTestWorkspace() {
        const std::filesystem::path temporaryRoot = std::filesystem::temp_directory_path();
        for (std::uint64_t attempt = 0; attempt < 32; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            std::filesystem::path candidate =
                temporaryRoot / ("asharia-project-bootstrap-test-" + std::to_string(stamp) + "-" +
                                 std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                return TestWorkspace{std::move(candidate)};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] asharia::project::AshariaProjectDescriptor makeDescriptor() {
        auto projectId = asharia::project::parseProjectId("9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21");
        return {
            .projectName = "Bootstrap Project",
            .projectId = projectId ? *projectId : asharia::project::ProjectId{},
            .assetSourceRoots =
                {
                    asharia::project::AssetSourceRootDesc{
                        .rootName = "assets",
                        .directory = "Assets",
                        .sourcePathPrefix = "Assets",
                    },
                    asharia::project::AssetSourceRootDesc{
                        .rootName = "plugins",
                        .directory = "Plugins",
                        .sourcePathPrefix = "Plugins",
                    },
                },
            .assetCacheRoot = ".asharia/cache/assets",
            .assetDiscovery = {.ignoredDirectoryNames = {".git", ".asharia"}},
        };
    }

    [[nodiscard]] bool testReaderAndSummary() {
        auto workspace = makeTestWorkspace();
        if (!workspace) {
            std::cerr << "Project Bootstrap test could not create a temporary directory.\n";
            return false;
        }

        const std::filesystem::path descriptorPath =
            workspace->root / std::string{asharia::project::kDefaultAshariaProjectFileName};
        auto written =
            asharia::project::writeAshariaProjectDescriptorFile(descriptorPath, makeDescriptor());
        if (!written) {
            std::cerr << written.error().message << '\n';
            return false;
        }

        auto summary = asharia::project_bootstrap::readProjectBootstrapSummaryV1(workspace->root);
        if (!summary || summary->projectName != "Bootstrap Project" ||
            summary->projectId != "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21" ||
            summary->assetSourceRootCount != 2) {
            std::cerr << "Project Bootstrap reader returned an unexpected summary.\n";
            return false;
        }

        auto rendered = asharia::project_bootstrap::renderProjectBootstrapSummaryJsonV1(*summary);
        constexpr std::string_view kExpected = R"json({
  "projectName": "Bootstrap Project",
  "projectId": "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21",
  "assetSourceRootCount": 2
}
)json";
        if (!rendered || *rendered != kExpected) {
            std::cerr << "Project Bootstrap summary JSON was not deterministic.\n";
            return false;
        }

        return true;
    }

    [[nodiscard]] bool testMissingDescriptorFails() {
        auto workspace = makeTestWorkspace();
        if (!workspace) {
            return false;
        }
        auto summary = asharia::project_bootstrap::readProjectBootstrapSummaryV1(workspace->root);
        return !summary &&
               summary.error().message.find("asharia.project.json") != std::string::npos;
    }

    struct BoundApplicationContext final {
        std::size_t invocationCount{};
        std::string diagnosticMessage{"borrowed diagnostic"};
    };

    [[nodiscard]] asharia::host_runtime::ProcessApplicationRunResultV1
    runBoundApplication(BoundApplicationContext& context,
                        std::span<const std::string_view> arguments) noexcept {
        ++context.invocationCount;
        if (arguments.size() == 1 && arguments.front() == "succeed") {
            return {
                .status = asharia::host_runtime::ProcessApplicationRunStatusV1::Succeeded,
                .exitCode = 0,
                .diagnosticCode = {},
                .diagnosticMessage = {},
            };
        }
        return {
            .status = asharia::host_runtime::ProcessApplicationRunStatusV1::Failed,
            .exitCode = 65,
            .diagnosticCode = "test.process-application.failed",
            .diagnosticMessage = context.diagnosticMessage,
        };
    }

    [[nodiscard]] bool testTypedProcessApplicationBinding() {
        static_assert(asharia::host_runtime::StaticContributionContractV1<
                      asharia::host_runtime::ProcessApplicationV1>);
        static_assert(asharia::host_runtime::ProcessApplicationV1::cardinality ==
                      asharia::host_runtime::StaticContributionCardinalityV1::Single);

        BoundApplicationContext context;
        auto application =
            asharia::host_runtime::bindProcessApplicationV1<BoundApplicationContext,
                                                            &runBoundApplication>(context);
        constexpr std::string_view kSuccessArgument{"succeed"};
        const auto success =
            application.run(std::span<const std::string_view>{&kSuccessArgument, 1});
        const auto failure = application.run(std::span<const std::string_view>{});
        return success.succeeded() && success.exitCode == 0 && !failure.succeeded() &&
               failure.exitCode == 65 &&
               failure.diagnosticCode == "test.process-application.failed" &&
               failure.diagnosticMessage == context.diagnosticMessage &&
               context.invocationCount == 2;
    }

} // namespace

// This boundary keeps all test exceptions inside the executable protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        if (!testReaderAndSummary() || !testMissingDescriptorFails() ||
            !testTypedProcessApplicationBinding()) {
            return 1;
        }
        std::cout << "Project Bootstrap tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Project Bootstrap tests threw: " << exception.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Project Bootstrap tests caught an unknown exception.\n";
        return 1;
    }
}
