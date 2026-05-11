#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <slang/slang-com-ptr.h>
#include <slang/slang.h>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

    // NOLINTBEGIN(modernize-raw-string-literal)

    struct Options {
        std::filesystem::path source;
        std::string entry;
        std::string stage;
        std::string profile;
        std::string target;
        std::filesystem::path output;
    };

    [[nodiscard]] std::string jsonEscape(std::string_view value) {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char character : value) {
            switch (character) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += character;
                break;
            }
        }
        return escaped;
    }

    [[nodiscard]] std::string blobString(ISlangBlob* blob) {
        if (blob == nullptr || blob->getBufferPointer() == nullptr || blob->getBufferSize() == 0) {
            return {};
        }

        const auto* chars = static_cast<const char*>(blob->getBufferPointer());
        return std::string{chars, blob->getBufferSize()};
    }

    [[nodiscard]] bool failed(SlangResult result) {
        return SLANG_FAILED(result);
    }

    [[nodiscard]] std::optional<SlangStage> parseStage(std::string_view stage) {
        if (stage == "vertex") {
            return SLANG_STAGE_VERTEX;
        }
        if (stage == "fragment") {
            return SLANG_STAGE_FRAGMENT;
        }
        if (stage == "compute") {
            return SLANG_STAGE_COMPUTE;
        }
        return std::nullopt;
    }

    [[nodiscard]] SlangCompileTarget parseTarget(std::string_view target) {
        if (target == "spirv") {
            return SLANG_SPIRV;
        }
        return SLANG_TARGET_UNKNOWN;
    }

    [[nodiscard]] std::string stageName(SlangStage stage) {
        switch (stage) {
        case SLANG_STAGE_VERTEX:
            return "vertex";
        case SLANG_STAGE_FRAGMENT:
            return "fragment";
        case SLANG_STAGE_COMPUTE:
            return "compute";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] std::string scalarTypeName(slang::TypeReflection::ScalarType type) {
        switch (type) {
        case slang::TypeReflection::ScalarType::Void:
            return "void";
        case slang::TypeReflection::ScalarType::Bool:
            return "bool";
        case slang::TypeReflection::ScalarType::Int32:
            return "int32";
        case slang::TypeReflection::ScalarType::UInt32:
            return "uint32";
        case slang::TypeReflection::ScalarType::Int64:
            return "int64";
        case slang::TypeReflection::ScalarType::UInt64:
            return "uint64";
        case slang::TypeReflection::ScalarType::Float16:
            return "float16";
        case slang::TypeReflection::ScalarType::Float32:
            return "float32";
        case slang::TypeReflection::ScalarType::Float64:
            return "float64";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] std::string typeName(slang::TypeReflection* type) {
        if (type == nullptr) {
            return "unknown";
        }

        Slang::ComPtr<ISlangBlob> fullName;
        if (!failed(type->getFullName(fullName.writeRef())) && fullName != nullptr) {
            auto name = blobString(fullName);
            if (!name.empty()) {
                return name;
            }
        }

        if (const char* name = type->getName();
            name != nullptr && !std::string_view{name}.empty()) {
            return name;
        }

        return "unknown";
    }

    [[nodiscard]] std::string parameterCategoryName(slang::ParameterCategory category) {
        switch (category) {
        case slang::ParameterCategory::ConstantBuffer:
            return "constantBuffer";
        case slang::ParameterCategory::ShaderResource:
            return "shaderResource";
        case slang::ParameterCategory::UnorderedAccess:
            return "unorderedAccess";
        case slang::ParameterCategory::VaryingInput:
            return "varyingInput";
        case slang::ParameterCategory::VaryingOutput:
            return "varyingOutput";
        case slang::ParameterCategory::SamplerState:
            return "samplerState";
        case slang::ParameterCategory::Uniform:
            return "uniform";
        case slang::ParameterCategory::PushConstantBuffer:
            return "pushConstantBuffer";
        default:
            return "other";
        }
    }

    [[nodiscard]] std::string bindingTypeName(slang::BindingType type) {
        switch (type) {
        case slang::BindingType::ConstantBuffer:
            return "constantBuffer";
        case slang::BindingType::Texture:
            return "texture";
        case slang::BindingType::Sampler:
            return "sampler";
        case slang::BindingType::MutableTexture:
            return "mutableTexture";
        case slang::BindingType::TypedBuffer:
            return "typedBuffer";
        case slang::BindingType::MutableTypedBuffer:
            return "mutableTypedBuffer";
        case slang::BindingType::RawBuffer:
            return "rawBuffer";
        case slang::BindingType::MutableRawBuffer:
            return "mutableRawBuffer";
        case slang::BindingType::CombinedTextureSampler:
            return "combinedTextureSampler";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] std::optional<Options> parseArgs(int argc, char** argv) {
        Options options;
        const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
        for (std::size_t index = 1; index < args.size(); ++index) {
            const std::string_view key{args[index]};
            if (index + 1 >= args.size()) {
                return std::nullopt;
            }
            const std::string value{args[++index]};
            if (key == "--source") {
                options.source = value;
            } else if (key == "--entry") {
                options.entry = value;
            } else if (key == "--stage") {
                options.stage = value;
            } else if (key == "--profile") {
                options.profile = value;
            } else if (key == "--target") {
                options.target = value;
            } else if (key == "--output") {
                options.output = value;
            } else {
                return std::nullopt;
            }
        }

        if (options.source.empty() || options.entry.empty() || options.stage.empty() ||
            options.profile.empty() || options.target.empty() || options.output.empty()) {
            return std::nullopt;
        }

        return options;
    }

    void writeVertexInputs(std::ostream& out, slang::EntryPointReflection* entryPoint,
                           bool includeInputs) {
        out << "  \"vertexInputs\": [\n";
        bool first = true;
        if (includeInputs) {
            for (unsigned index = 0; index < entryPoint->getParameterCount(); ++index) {
                auto* parameter = entryPoint->getParameterByIndex(index);
                if (parameter == nullptr ||
                    parameter->getCategory() != slang::ParameterCategory::VaryingInput) {
                    continue;
                }

                if (!first) {
                    out << ",\n";
                }
                first = false;

                auto* type = parameter->getType();
                out << "    {\n";
                out << "      \"name\": \"" << jsonEscape(parameter->getName()) << "\",\n";
                out << "      \"location\": " << parameter->getBindingIndex() << ",\n";
                out << "      \"semantic\": \"" << jsonEscape(parameter->getSemanticName())
                    << "\",\n";
                out << "      \"semanticIndex\": " << parameter->getSemanticIndex() << ",\n";
                out << "      \"type\": \"" << jsonEscape(typeName(type)) << "\",\n";
                out << "      \"scalarType\": \""
                    << jsonEscape(scalarTypeName(type->getScalarType())) << "\",\n";
                out << "      \"rowCount\": " << type->getRowCount() << ",\n";
                out << "      \"columnCount\": " << type->getColumnCount() << "\n";
                out << "    }";
            }
        }
        out << "\n  ],\n";
    }

    void writeDescriptorBindings(std::ostream& out, slang::ProgramLayout* layout) {
        out << "  \"descriptorBindings\": [\n";
        bool first = true;
        for (unsigned parameterIndex = 0; parameterIndex < layout->getParameterCount();
             ++parameterIndex) {
            auto* parameter = layout->getParameterByIndex(parameterIndex);
            if (parameter == nullptr) {
                continue;
            }
            auto* typeLayout = parameter->getTypeLayout();
            if (typeLayout == nullptr) {
                continue;
            }

            const SlangInt rangeCount = typeLayout->getBindingRangeCount();
            for (SlangInt rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex) {
                const auto bindingType = typeLayout->getBindingRangeType(rangeIndex);
                if (bindingType == slang::BindingType::ExistentialValue) {
                    continue;
                }

                if (!first) {
                    out << ",\n";
                }
                first = false;

                out << "    {\n";
                out << "      \"name\": \"" << jsonEscape(parameter->getName()) << "\",\n";
                out << "      \"set\": "
                    << typeLayout->getBindingRangeDescriptorSetIndex(rangeIndex) << ",\n";
                out << "      \"binding\": " << parameter->getBindingIndex() << ",\n";
                out << "      \"kind\": \"" << jsonEscape(bindingTypeName(bindingType)) << "\",\n";
                out << "      \"count\": " << typeLayout->getBindingRangeBindingCount(rangeIndex)
                    << ",\n";
                out << "      \"category\": \""
                    << jsonEscape(parameterCategoryName(parameter->getCategory())) << "\"\n";
                out << "    }";
            }
        }
        out << "\n  ],\n";
    }

    void writePushConstants(std::ostream& out, slang::ProgramLayout* layout) {
        out << "  \"pushConstants\": [\n";
        bool first = true;
        for (unsigned parameterIndex = 0; parameterIndex < layout->getParameterCount();
             ++parameterIndex) {
            auto* parameter = layout->getParameterByIndex(parameterIndex);
            if (parameter == nullptr ||
                parameter->getCategory() != slang::ParameterCategory::PushConstantBuffer) {
                continue;
            }

            if (!first) {
                out << ",\n";
            }
            first = false;

            out << "    {\n";
            out << "      \"name\": \"" << jsonEscape(parameter->getName()) << "\",\n";
            out << "      \"offset\": "
                << parameter->getOffset(slang::ParameterCategory::PushConstantBuffer) << ",\n";
            out << "      \"size\": "
                << parameter->getTypeLayout()->getSize(slang::ParameterCategory::Uniform) << ",\n";
            out << "      \"stage\": \"" << jsonEscape(stageName(parameter->getStage())) << "\"\n";
            out << "    }";
        }
        out << "\n  ],\n";
    }

    [[nodiscard]] int writeReflection(const Options& options) {
        const auto stage = parseStage(options.stage);
        if (!stage) {
            std::cerr << "Unsupported Slang shader stage: " << options.stage << '\n';
            return 1;
        }

        const SlangCompileTarget target = parseTarget(options.target);
        if (target == SLANG_TARGET_UNKNOWN) {
            std::cerr << "Unsupported Slang reflection target: " << options.target << '\n';
            return 1;
        }

        Slang::ComPtr<slang::IGlobalSession> globalSession;
        if (failed(slang::createGlobalSession(globalSession.writeRef()))) {
            std::cerr << "Failed to create Slang global session.\n";
            return 1;
        }

        slang::TargetDesc targetDesc{};
        targetDesc.format = target;
        targetDesc.profile = globalSession->findProfile(options.profile.c_str());
        if (targetDesc.profile == SLANG_PROFILE_UNKNOWN) {
            std::cerr << "Slang profile is unknown: " << options.profile << '\n';
            return 1;
        }

        const auto sourcePath = std::filesystem::absolute(options.source).lexically_normal();
        const auto sourceDir = sourcePath.parent_path().string();
        const std::array<const char*, 1> searchPaths{sourceDir.c_str()};

        slang::SessionDesc sessionDesc{};
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;
        sessionDesc.searchPaths = searchPaths.data();
        sessionDesc.searchPathCount = static_cast<SlangInt>(searchPaths.size());

        Slang::ComPtr<slang::ISession> session;
        if (failed(globalSession->createSession(sessionDesc, session.writeRef()))) {
            std::cerr << "Failed to create Slang session.\n";
            return 1;
        }

        Slang::ComPtr<slang::IBlob> diagnostics;
        const auto moduleName = sourcePath.stem().string();
        Slang::ComPtr<slang::IModule> module;
        module = session->loadModule(moduleName.c_str(), diagnostics.writeRef());
        if (module == nullptr) {
            std::cerr << "Failed to load Slang module: " << sourcePath.string() << '\n'
                      << blobString(diagnostics) << '\n';
            return 1;
        }

        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        diagnostics.setNull();
        if (failed(module->findAndCheckEntryPoint(options.entry.c_str(), *stage,
                                                  entryPoint.writeRef(), diagnostics.writeRef()))) {
            std::cerr << "Failed to find Slang entry point: " << options.entry << '\n'
                      << blobString(diagnostics) << '\n';
            return 1;
        }

        const std::array<slang::IComponentType*, 2> components{module, entryPoint};
        Slang::ComPtr<slang::IComponentType> program;
        diagnostics.setNull();
        if (failed(session->createCompositeComponentType(
                components.data(), static_cast<SlangInt>(components.size()), program.writeRef(),
                diagnostics.writeRef()))) {
            std::cerr << "Failed to compose Slang program for reflection.\n"
                      << blobString(diagnostics) << '\n';
            return 1;
        }

        diagnostics.setNull();
        slang::ProgramLayout* layout = program->getLayout(0, diagnostics.writeRef());
        if (layout == nullptr) {
            std::cerr << "Failed to get Slang program layout.\n" << blobString(diagnostics) << '\n';
            return 1;
        }

        auto* reflectedEntryPoint = layout->findEntryPointByName(options.entry.c_str());
        if (reflectedEntryPoint == nullptr) {
            std::cerr << "Slang reflection did not include entry point: " << options.entry << '\n';
            return 1;
        }

        std::filesystem::create_directories(options.output.parent_path());
        std::ofstream out{options.output, std::ios::binary};
        if (!out) {
            std::cerr << "Failed to open reflection output: " << options.output.string() << '\n';
            return 1;
        }

        out << "{\n";
        out << "  \"source\": \"" << jsonEscape(sourcePath.string()) << "\",\n";
        out << "  \"entry\": \"" << jsonEscape(options.entry) << "\",\n";
        out << "  \"stage\": \"" << jsonEscape(options.stage) << "\",\n";
        out << "  \"profile\": \"" << jsonEscape(options.profile) << "\",\n";
        out << "  \"target\": \"" << jsonEscape(options.target) << "\",\n";
        out << "  \"compiler\": {\n";
        out << "    \"name\": \"slang-api\",\n";
        out << "    \"version\": \"" << jsonEscape(globalSession->getBuildTagString()) << "\"\n";
        out << "  },\n";
        writeVertexInputs(out, reflectedEntryPoint, *stage == SLANG_STAGE_VERTEX);
        writeDescriptorBindings(out, layout);
        writePushConstants(out, layout);
        out << "  \"entryPoint\": {\n";
        out << "    \"name\": \"" << jsonEscape(reflectedEntryPoint->getName()) << "\",\n";
        out << "    \"stage\": \"" << jsonEscape(stageName(reflectedEntryPoint->getStage()))
            << "\",\n";
        out << "    \"parameterCount\": " << reflectedEntryPoint->getParameterCount() << "\n";
        out << "  }\n";
        out << "}\n";
        return 0;
    }

    // NOLINTEND(modernize-raw-string-literal)

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    try {
        const auto options = parseArgs(argc, argv);
        if (!options) {
            std::cerr << "Usage: asharia-slang-reflect --source <file> --entry <name> --stage <stage> "
                         "--profile <profile> --target <target> --output <file>\n";
            return 1;
        }

        return writeReflection(*options);
    } catch (const std::exception& exception) {
        std::cerr << "Unhandled reflection error: " << exception.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Unhandled non-standard reflection error.\n";
        return 1;
    }
}
