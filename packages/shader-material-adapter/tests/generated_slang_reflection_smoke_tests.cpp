#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "asharia/shader_authoring/ashader_generated_slang.hpp"
#include "asharia/shader_authoring/ashader_parser.hpp"
#include "asharia/shader_material_adapter/reflection_to_material_signature.hpp"

namespace {

    struct Options {
        std::filesystem::path slangc;
        std::filesystem::path spirvVal;
        std::filesystem::path reflect;
        std::filesystem::path workDir;
    };

    void logFailure(std::string_view message) {
        std::cerr << message << '\n';
    }

    [[nodiscard]] std::string quotePath(const std::filesystem::path& path) {
        return "\"" + path.string() + "\"";
    }

    [[nodiscard]] std::optional<Options> parseArgs(int argc, char** argv) {
        Options options;
        const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        for (std::size_t index = 1; index < args.size(); ++index) {
            const std::string_view key{args[index]};
            if (index + 1 >= args.size()) {
                return std::nullopt;
            }
            const std::filesystem::path value{args[++index]};
            if (key == "--slangc") {
                options.slangc = value;
            } else if (key == "--spirv-val") {
                options.spirvVal = value;
            } else if (key == "--reflect") {
                options.reflect = value;
            } else if (key == "--work-dir") {
                options.workDir = value;
            } else {
                return std::nullopt;
            }
        }

        if (options.slangc.empty() || options.spirvVal.empty() || options.reflect.empty() ||
            options.workDir.empty()) {
            return std::nullopt;
        }
        return options;
    }

    [[nodiscard]] bool runCommand(std::string_view label, const std::string& command) {
#if defined(_WIN32)
        const std::string shellCommand = "cmd /S /C \"" + command + "\"";
#else
        const std::string shellCommand = command;
#endif
        const int exitCode = std::system(shellCommand.c_str());
        if (exitCode != 0) {
            logFailure(std::string{label} + " failed with exit code " + std::to_string(exitCode));
            return false;
        }
        return true;
    }

    [[nodiscard]] bool writeTextFile(const std::filesystem::path& path, std::string_view text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file{path, std::ios::binary};
        if (!file) {
            logFailure("Failed to open generated Slang output: " + path.string());
            return false;
        }
        file << text;
        return true;
    }

    [[nodiscard]] std::string expectedKind(asharia::shader_authoring::AshaderPropertyType type) {
        switch (type) {
        case asharia::shader_authoring::AshaderPropertyType::Texture2D:
            return "texture";
        case asharia::shader_authoring::AshaderPropertyType::Sampler:
            return "sampler";
        case asharia::shader_authoring::AshaderPropertyType::Float:
        case asharia::shader_authoring::AshaderPropertyType::Float2:
        case asharia::shader_authoring::AshaderPropertyType::Float3:
        case asharia::shader_authoring::AshaderPropertyType::Float4:
        case asharia::shader_authoring::AshaderPropertyType::Color:
        case asharia::shader_authoring::AshaderPropertyType::Int:
        case asharia::shader_authoring::AshaderPropertyType::UInt:
        case asharia::shader_authoring::AshaderPropertyType::Bool:
            return "constantBuffer";
        }
        return "unknown";
    }

    [[nodiscard]] const asharia::ShaderDescriptorBindingReflection*
    findDescriptor(const asharia::ShaderResourceSignature& signature, std::uint32_t set,
                   std::uint32_t binding) {
        for (const auto& descriptor : signature.descriptorBindings) {
            if (descriptor.set == set && descriptor.binding == binding) {
                return &descriptor;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<std::string> validateGeneratedReflectionContract(
        const asharia::shader_authoring::GeneratedSlangResult& generated,
        const asharia::shader_authoring::GeneratedSlangOptions& options,
        const asharia::ShaderResourceSignature& reflected) {
        const auto* materialDescriptor =
            findDescriptor(reflected, options.materialSet, options.materialParameterBinding);
        if (materialDescriptor == nullptr) {
            return "missing reflected material parameter descriptor";
        }
        if (materialDescriptor->name != options.materialParameterBindingName ||
            materialDescriptor->kind != "constantBuffer" || materialDescriptor->count != 1U) {
            return "reflected material parameter descriptor does not match generated binding";
        }

        for (const auto& binding : generated.bindings) {
            if (binding.inMaterialParameterBlock) {
                continue;
            }

            const auto* descriptor = findDescriptor(reflected, binding.set, binding.binding);
            if (descriptor == nullptr) {
                return "missing reflected descriptor for generated binding: " + binding.name;
            }
            if (descriptor->name != binding.name ||
                descriptor->kind != expectedKind(binding.type) || descriptor->count != 1U) {
                return "reflected descriptor does not match generated binding: " + binding.name;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool
    compileAndReflectEntry(const Options& options, const std::filesystem::path& source,
                           const asharia::shader_authoring::GeneratedSlangEntryPoint& entryPoint) {
        const std::string_view entry = entryPoint.compileEntryName;
        const std::string_view stage = asharia::shader_authoring::toString(entryPoint.stage);
        const std::filesystem::path spirvPath =
            options.workDir / (std::string{entry} + "." + std::string{stage} + ".spv");
        const std::filesystem::path reflectionPath =
            options.workDir / (std::string{entry} + "." + std::string{stage} + ".reflection.json");

        const std::string compileCommand = quotePath(options.slangc) + " " + quotePath(source) +
                                           " -profile glsl_450 -target spirv -entry " +
                                           std::string{entry} + " -stage " + std::string{stage} +
                                           " -o " + quotePath(spirvPath);
        if (!runCommand("slangc " + std::string{stage}, compileCommand)) {
            return false;
        }

        if (!runCommand("spirv-val " + std::string{stage},
                        quotePath(options.spirvVal) + " " + quotePath(spirvPath))) {
            return false;
        }

        const std::string reflectionCommand =
            quotePath(options.reflect) + " --source " + quotePath(source) + " --entry " +
            std::string{entry} + " --stage " + std::string{stage} +
            " --profile glsl_450 --target spirv --output " + quotePath(reflectionPath);
        return runCommand("asharia-slang-reflect " + std::string{stage}, reflectionCommand);
    }

    [[nodiscard]] const asharia::shader_authoring::GeneratedSlangEntryPoint*
    findEntryPoint(const asharia::shader_authoring::GeneratedSlangResult& generated,
                   asharia::shader_authoring::GeneratedSlangStage stage) {
        for (const auto& entryPoint : generated.entryPoints) {
            if (entryPoint.stage == stage) {
                return &entryPoint;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool smokeGeneratedSlangCompileReflection(const Options& options) {
        constexpr std::string_view kSource = R"ashader(
schema 2

shader "asharia.material.generated_reflection" {
  properties {
    color baseColor = [1, 1, 1, 1]
    texture2D albedoMap
    sampler linearSampler
    float roughness = 0.5
  }

  pass "Forward" {
    tag "SceneForward"
    vertex vertexMain
    fragment fragmentMain
  }

  slang {
    struct VertexOutput {
      float4 position : SV_Position;
    };

    VertexOutput vertexMain() {
      VertexOutput output;
      output.position = float4(0.0, 0.0, 0.0, 1.0);
      return output;
    }

    float4 fragmentMain() : SV_Target {
      return Material.baseColor;
    }
  }
}
)ashader";

        const auto parsed = asharia::shader_authoring::parseAshaderDocument(kSource);
        if (!parsed.document || asharia::shader_authoring::hasErrors(parsed.diagnostics)) {
            logFailure("Generated Slang reflection smoke fixture failed to parse.");
            return false;
        }

        const asharia::shader_authoring::GeneratedSlangOptions generatedOptions{
            .sourceName = "GeneratedReflection.ashader",
            .generatedName = "GeneratedReflection.generated.slang",
        };
        const auto generated =
            asharia::shader_authoring::buildGeneratedSlang(*parsed.document, generatedOptions);
        if (asharia::shader_authoring::hasErrors(generated.diagnostics)) {
            logFailure("Generated Slang reflection smoke produced builder diagnostics.");
            return false;
        }

        const std::filesystem::path generatedPath =
            options.workDir / generatedOptions.generatedName;
        if (!writeTextFile(generatedPath, generated.source)) {
            return false;
        }

        const auto* vertexEntry =
            findEntryPoint(generated, asharia::shader_authoring::GeneratedSlangStage::Vertex);
        const auto* fragmentEntry =
            findEntryPoint(generated, asharia::shader_authoring::GeneratedSlangStage::Fragment);
        if (vertexEntry == nullptr || fragmentEntry == nullptr ||
            vertexEntry->sourceEntryName != "vertexMain" ||
            vertexEntry->compileEntryName != "vertexMain" ||
            vertexEntry->generatedWrapperName != "__asharia_Forward_vertex" ||
            fragmentEntry->sourceEntryName != "fragmentMain" ||
            fragmentEntry->compileEntryName != "fragmentMain" ||
            fragmentEntry->generatedWrapperName != "__asharia_Forward_fragment") {
            logFailure("Generated Slang reflection smoke did not produce expected entry manifest.");
            return false;
        }

        if (!compileAndReflectEntry(options, generatedPath, *vertexEntry) ||
            !compileAndReflectEntry(options, generatedPath, *fragmentEntry)) {
            return false;
        }

        auto vertexReflection =
            asharia::readShaderReflection(options.workDir / "vertexMain.vertex.reflection.json");
        if (!vertexReflection) {
            logFailure(vertexReflection.error().message);
            return false;
        }
        auto fragmentReflection = asharia::readShaderReflection(
            options.workDir / "fragmentMain.fragment.reflection.json");
        if (!fragmentReflection) {
            logFailure(fragmentReflection.error().message);
            return false;
        }

        const std::array<asharia::ShaderReflection, 2> reflections{
            std::move(*vertexReflection),
            std::move(*fragmentReflection),
        };
        const auto signature =
            asharia::shaderResourceSignature(std::span<const asharia::ShaderReflection>{
                reflections.data(),
                reflections.size(),
            });

        if (const auto mismatch =
                validateGeneratedReflectionContract(generated, generatedOptions, signature)) {
            logFailure(*mismatch);
            return false;
        }

        auto adapted = asharia::shader_material::makeReflectionMaterialSignature(signature);
        if (!adapted) {
            logFailure(adapted.error().message);
            return false;
        }
        if (adapted->signatureHash == 0 || adapted->signature.bindings.size() != 3U) {
            logFailure("Generated Slang reflection smoke produced incomplete material signature.");
            return false;
        }

        auto mismatchedSignature = signature;
        mismatchedSignature.descriptorBindings[1].binding = 9U;
        if (!validateGeneratedReflectionContract(generated, generatedOptions,
                                                 mismatchedSignature)) {
            logFailure("Generated/reflection mismatch smoke failed to report a mismatch.");
            return false;
        }

        std::cout << "Generated Slang reflection signature hash: " << adapted->signatureHash
                  << '\n';
        return true;
    }

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    try {
        const auto options = parseArgs(argc, argv);
        if (!options) {
            logFailure("Usage: asharia-generated-slang-reflection-smoke-tests "
                       "--slangc <path> --spirv-val <path> --reflect <path> --work-dir <path>");
            return EXIT_FAILURE;
        }

        if (!smokeGeneratedSlangCompileReflection(*options)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        logFailure(exception.what());
        return EXIT_FAILURE;
    } catch (...) {
        logFailure("Generated Slang reflection smoke caught an unknown exception.");
        return EXIT_FAILURE;
    }
}
