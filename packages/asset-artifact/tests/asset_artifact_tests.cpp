#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/asset_artifact/asset_artifact_v1.hpp"
#include "asharia/core/file_io.hpp"

namespace {

    class ScopedDirectory final {
    public:
        ScopedDirectory()
            : path_(std::filesystem::temp_directory_path() / "asharia-asset-artifact-tests-fixed") {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
            std::filesystem::create_directories(path_ / "products", error);
        }

        ~ScopedDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScopedDirectory(const ScopedDirectory&) = delete;
        ScopedDirectory& operator=(const ScopedDirectory&) = delete;
        ScopedDirectory(ScopedDirectory&&) = delete;
        ScopedDirectory& operator=(ScopedDirectory&&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] bool contains(std::string_view text, std::string_view token) {
        return text.find(token) != std::string_view::npos;
    }

    [[nodiscard]] bool expectCode(const asharia::Error& error,
                                  asharia::asset::AssetArtifactErrorCode code,
                                  std::string_view token, const std::filesystem::path& hiddenRoot) {
        const std::string rootText = hiddenRoot.generic_string();
        if (error.domain != asharia::ErrorDomain::Asset || error.code != static_cast<int>(code) ||
            !contains(error.message, token) || contains(error.message, rootText)) {
            std::cerr << "Unexpected artifact diagnostic: " << error.message << '\n';
            return false;
        }
        return true;
    }

    [[nodiscard]] asharia::asset::AssetProductRecord makeProduct(std::span<const std::byte> bytes) {
        asharia::asset::AssetProductKey key{
            .guid = asharia::asset::AssetGuid{.bytes = {0x42U}},
            .assetType = asharia::asset::makeAssetTypeId("com.asharia.asset.Mesh"),
            .importerId = asharia::asset::makeImporterId("com.asharia.importer.test"),
            .importerVersion = asharia::asset::ImporterVersion{1U},
            .sourceHash = 1U,
            .settingsHash = 2U,
            .dependencyHash = 0U,
            .targetProfileHash = 3U,
        };
        return asharia::asset::AssetProductRecord{
            .key = key,
            .relativeProductPath = "products/fixture.mesh",
            .productSizeBytes = bytes.size(),
            .productHash = asharia::asset::hashAssetArtifactBytesV1(bytes),
        };
    }

    [[nodiscard]] bool pathTests(const std::filesystem::path& root) {
        constexpr std::array<std::string_view, 8U> kInvalidPaths{
            "", "\\absolute", "/absolute", "C:/absolute", "a//b", "./a", "a/../b", "a/"};
        for (const std::string_view path : kInvalidPaths) {
            auto result = asharia::asset::validateAssetArtifactRelativePathV1(path);
            if (result ||
                !expectCode(result.error(), asharia::asset::AssetArtifactErrorCode::InvalidPath,
                            "invalid", root)) {
                return false;
            }
        }
        return static_cast<bool>(
            asharia::asset::validateAssetArtifactRelativePathV1("products/mesh.amesh"));
    }

    [[nodiscard]] bool readTests(const ScopedDirectory& directory) {
        const std::vector<std::byte> bytes{std::byte{0x10U}, std::byte{0x20U}, std::byte{0x30U}};
        const auto product = makeProduct(bytes);
        auto locator = asharia::asset::makeAssetArtifactLocatorV1(product);
        if (!locator) {
            std::cerr << locator.error().message << '\n';
            return false;
        }

        auto missing = asharia::asset::readVerifiedAssetArtifactV1(directory.path(), *locator);
        if (missing ||
            !expectCode(missing.error(), asharia::asset::AssetArtifactErrorCode::FileReadFailed,
                        product.relativeProductPath, directory.path())) {
            return false;
        }

        auto tooSmall = asharia::asset::readVerifiedAssetArtifactV1(
            directory.path(), *locator, {.maxBytes = bytes.size() - 1U});
        if (tooSmall || !expectCode(tooSmall.error(),
                                    asharia::asset::AssetArtifactErrorCode::ByteBudgetExceeded,
                                    "exceeds", directory.path())) {
            return false;
        }

        const std::filesystem::path file = directory.path() / product.relativeProductPath;
        if (auto written = asharia::core::writeFileBytesAtomically(file, bytes); !written) {
            std::cerr << written.error().message << '\n';
            return false;
        }

        auto wrongSizeLocator = *locator;
        ++wrongSizeLocator.expectedBytes;
        auto wrongSize =
            asharia::asset::readVerifiedAssetArtifactV1(directory.path(), wrongSizeLocator);
        if (wrongSize ||
            !expectCode(wrongSize.error(), asharia::asset::AssetArtifactErrorCode::SizeMismatch,
                        "actualBytes", directory.path())) {
            return false;
        }

        auto wrongHashLocator = *locator;
        ++wrongHashLocator.expectedHash;
        auto wrongHash =
            asharia::asset::readVerifiedAssetArtifactV1(directory.path(), wrongHashLocator);
        if (wrongHash ||
            !expectCode(wrongHash.error(), asharia::asset::AssetArtifactErrorCode::HashMismatch,
                        "actualHash", directory.path())) {
            return false;
        }

        auto verified = asharia::asset::readVerifiedAssetArtifactV1(directory.path(), *locator);
        if (!verified || verified->locator != *locator || verified->bytes != bytes) {
            std::cerr << (verified ? "Verified artifact facts differed.\n"
                                   : verified.error().message + "\n");
            return false;
        }
        return true;
    }

} // namespace

// Unexpected exceptions are reported by the test executable rather than escaping main.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        const ScopedDirectory directory;
        if (!pathTests(directory.path()) || !readTests(directory)) {
            return EXIT_FAILURE;
        }
        std::cout << "Asset artifact tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Asset artifact tests threw: " << exception.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Asset artifact tests caught an unknown exception.\n";
        return EXIT_FAILURE;
    }
}
