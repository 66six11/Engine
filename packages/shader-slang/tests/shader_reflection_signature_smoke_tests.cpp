#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/shader_slang/reflection.hpp"

namespace {

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    [[nodiscard]] bool smokeReflectionReadLimit() {
        const std::string json =
            R"({"source":"limit.slang","entry":"main","stage":"vertex","profile":"glsl_450","target":"spirv","vertexInputs":[],"descriptorBindings":[],"pushConstants":[]})";
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "asharia-shader-reflection-limit.json";
        {
            std::ofstream file{path, std::ios::binary | std::ios::trunc};
            file.write(json.data(), static_cast<std::streamsize>(json.size()));
        }

        const auto exact = asharia::readShaderReflection(
            path, asharia::ShaderReflectionFileOptions{.maxBytes = json.size()});
        const auto over = asharia::readShaderReflection(
            path, asharia::ShaderReflectionFileOptions{.maxBytes = json.size() - 1U});
        std::error_code removeError;
        std::filesystem::remove(path, removeError);

        if (!exact) {
            logFailure("Shader reflection exact-limit read failed: " + exact.error().message);
            return false;
        }
        if (over || over.error().message.find("shader reflection JSON") == std::string::npos ||
            over.error().message.find("observedBytes=") == std::string::npos ||
            over.error().message.find("maxBytes=") == std::string::npos) {
            logFailure(
                "Shader reflection one-byte-over limit did not preserve domain/Core context.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool messageContains(std::string_view message, std::string_view token) {
        return message.find(token) != std::string_view::npos;
    }

    [[nodiscard]] asharia::ShaderDescriptorBindingReflection
    descriptor(std::string name, std::uint32_t set, std::uint32_t binding, std::string kind,
               std::uint32_t count, std::string category, std::string stageVisibility) {
        return asharia::ShaderDescriptorBindingReflection{
            .name = std::move(name),
            .set = set,
            .binding = binding,
            .kind = std::move(kind),
            .count = count,
            .category = std::move(category),
            .stageVisibility = std::move(stageVisibility),
        };
    }

    [[nodiscard]] asharia::ShaderPushConstantReflection pushConstant(std::string name,
                                                                     std::uint32_t offset,
                                                                     std::uint32_t size,
                                                                     std::string stageVisibility) {
        return asharia::ShaderPushConstantReflection{
            .name = std::move(name),
            .offset = offset,
            .size = size,
            .stageVisibility = std::move(stageVisibility),
        };
    }

    [[nodiscard]] asharia::ShaderReflection
    shader(std::string stage, std::vector<asharia::ShaderDescriptorBindingReflection> descriptors,
           std::vector<asharia::ShaderPushConstantReflection> pushConstants = {}) {
        asharia::ShaderReflection reflection{
            .source = "ReflectionMergeTest.slang",
            .entry = stage + "Main",
            .stage = std::move(stage),
            .profile = "glsl_450",
            .target = "spirv",
            .vertexInputs = {},
            .descriptorBindings = std::move(descriptors),
            .pushConstants = std::move(pushConstants),
        };
        reflection.descriptorBindingCount =
            static_cast<std::uint32_t>(reflection.descriptorBindings.size());
        reflection.pushConstantCount = static_cast<std::uint32_t>(reflection.pushConstants.size());
        return reflection;
    }

    [[nodiscard]] bool
    expectMergeError(const asharia::Result<asharia::ShaderResourceSignature>& signature,
                     std::string_view expectedToken) {
        if (signature) {
            logFailure("Shader reflection merge accepted a conflicting resource contract.");
            return false;
        }
        if (signature.error().domain != asharia::ErrorDomain::Shader ||
            !messageContains(signature.error().message, expectedToken)) {
            logFailure(std::string{"Shader reflection merge produced incomplete diagnostic: "} +
                       signature.error().message);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool
    expectCompatibilityMergeError(const asharia::ShaderResourceSignature& signature,
                                  std::string_view expectedToken) {
        if (!signature.error) {
            logFailure("Shader reflection merge accepted a conflicting resource contract.");
            return false;
        }
        if (signature.error->domain != asharia::ErrorDomain::Shader ||
            !messageContains(signature.error->message, expectedToken)) {
            logFailure(std::string{"Shader reflection merge produced incomplete diagnostic: "} +
                       signature.error->message);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool smokeCompatibleDescriptorsMergeVisibility() {
        const std::array shaders{
            shader("vertex",
                   {descriptor("sceneParams", 0, 0, "constantBuffer", 1, "descriptor", "vertex")}),
            shader("fragment", {descriptor("sceneParams", 0, 0, "constantBuffer", 1, "descriptor",
                                           "fragment")}),
        };

        const asharia::Result<asharia::ShaderResourceSignature> signature =
            asharia::mergeShaderResourceSignature(
                std::span<const asharia::ShaderReflection>{shaders});
        if (!signature) {
            logFailure(signature.error().message);
            return false;
        }
        if (signature->descriptorBindings.size() != 1 || signature->descriptorBindingCount != 1 ||
            signature->descriptorBindings.front().stageVisibility != "vertex|fragment") {
            logFailure("Shader reflection merge did not combine compatible descriptor stages.");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool smokeDescriptorConflictsFail() {
        const std::array kindMismatch{
            shader("vertex",
                   {descriptor("sceneParams", 0, 0, "constantBuffer", 1, "descriptor", "vertex")}),
            shader("fragment",
                   {descriptor("sceneParams", 0, 0, "texture", 1, "descriptor", "fragment")}),
        };

        const std::array countMismatch{
            shader("vertex",
                   {descriptor("sceneParams", 0, 0, "constantBuffer", 1, "descriptor", "vertex")}),
            shader("fragment", {descriptor("sceneParams", 0, 0, "constantBuffer", 2, "descriptor",
                                           "fragment")}),
        };

        const std::array nameMismatch{
            shader("vertex",
                   {descriptor("sceneParams", 0, 0, "constantBuffer", 1, "descriptor", "vertex")}),
            shader("fragment", {descriptor("materialParams", 0, 0, "constantBuffer", 1,
                                           "descriptor", "fragment")}),
        };

        return expectMergeError(asharia::mergeShaderResourceSignature(
                                    std::span<const asharia::ShaderReflection>{kindMismatch}),
                                "descriptor binding conflict") &&
               expectMergeError(asharia::mergeShaderResourceSignature(
                                    std::span<const asharia::ShaderReflection>{countMismatch}),
                                "descriptor binding conflict") &&
               expectMergeError(asharia::mergeShaderResourceSignature(
                                    std::span<const asharia::ShaderReflection>{nameMismatch}),
                                "descriptor binding conflict") &&
               expectCompatibilityMergeError(
                   asharia::shaderResourceSignature(
                       std::span<const asharia::ShaderReflection>{nameMismatch}),
                   "descriptor binding conflict");
    }

    [[nodiscard]] bool smokePushConstantConflictsFail() {
        const std::array compatible{
            shader("vertex", {}, {pushConstant("DrawParams", 0, 16, "vertex")}),
            shader("fragment", {}, {pushConstant("DrawParams", 0, 16, "fragment")}),
        };

        const asharia::Result<asharia::ShaderResourceSignature> compatibleSignature =
            asharia::mergeShaderResourceSignature(
                std::span<const asharia::ShaderReflection>{compatible});
        if (!compatibleSignature || compatibleSignature->pushConstants.size() != 1 ||
            compatibleSignature->pushConstantCount != 1 ||
            compatibleSignature->pushConstants.front().stageVisibility != "vertex|fragment") {
            logFailure("Shader reflection merge did not combine compatible push constants.");
            return false;
        }

        const std::array nameMismatch{
            shader("vertex", {}, {pushConstant("DrawParams", 0, 16, "vertex")}),
            shader("fragment", {}, {pushConstant("MaterialParams", 0, 16, "fragment")}),
        };

        const std::array overlappingRange{
            shader("vertex", {}, {pushConstant("DrawParams", 0, 16, "vertex")}),
            shader("fragment", {}, {pushConstant("MaterialParams", 8, 16, "fragment")}),
        };

        return expectMergeError(asharia::mergeShaderResourceSignature(
                                    std::span<const asharia::ShaderReflection>{nameMismatch}),
                                "push constant conflict") &&
               expectMergeError(asharia::mergeShaderResourceSignature(
                                    std::span<const asharia::ShaderReflection>{overlappingRange}),
                                "push constant conflict");
    }

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    try {
        if (!smokeReflectionReadLimit() || !smokeCompatibleDescriptorsMergeVisibility() ||
            !smokeDescriptorConflictsFail() || !smokePushConstantConflictsFail()) {
            return EXIT_FAILURE;
        }
    } catch (const std::exception& exception) {
        logFailure(exception.what());
        return EXIT_FAILURE;
    } catch (...) {
        logFailure("Shader reflection merge smoke caught an unknown exception.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
