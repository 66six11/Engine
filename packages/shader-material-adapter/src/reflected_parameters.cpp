#include "asharia/shader_material_adapter/reflected_parameters.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace asharia::shader_material {
    namespace {
        using shader_authoring::AshaderPropertyType;

        Error layoutError(std::string_view property, std::string_view reason) {
            return {ErrorDomain::Material,
                    static_cast<int>(material_instance::AmatParameterError::InvalidLayout),
                    "Reflected material parameter '" + std::string{property} +
                        "': " + std::string{reason}};
        }

        bool matchesType(AshaderPropertyType type, const ShaderParameterMemberReflection& member) {
            std::string_view scalar = "float32";
            std::uint32_t count = 1;
            switch (type) {
            case AshaderPropertyType::Float:
                break;
            case AshaderPropertyType::Float2:
                count = 2;
                break;
            case AshaderPropertyType::Float3:
                count = 3;
                break;
            case AshaderPropertyType::Float4:
            case AshaderPropertyType::Color:
                count = 4;
                break;
            case AshaderPropertyType::Int:
                scalar = "int32";
                break;
            case AshaderPropertyType::UInt:
                scalar = "uint32";
                break;
            case AshaderPropertyType::Bool:
                scalar = "bool";
                break;
            default:
                return false;
            }
            return member.scalarType == scalar && member.componentCount == count &&
                   member.size == count * 4;
        }
    } // namespace

    Result<ReflectedMaterialParameters>
    packReflectedMaterialParameters(const material_instance::AmatDocument& document,
                                    const shader_authoring::AshaderDocument& shader,
                                    const ShaderDescriptorBindingReflection& binding,
                                    const material_instance::AmatResolveOptions& options) {
        if (binding.kind != "constantBuffer" || binding.count != 1 || !binding.parameterBlock) {
            return std::unexpected{
                layoutError(binding.name, "expected one reflected constant buffer")};
        }
        const auto& layout = *binding.parameterBlock;
        if (layout.members.size() > material_instance::kMaxAmatParameters ||
            layout.size > material_instance::kMaxAmatParameterBytes ||
            layout.members.size() != shader.properties.size()) {
            return std::unexpected{layoutError(binding.name, "layout coverage or budget mismatch")};
        }
        std::vector<material_instance::AmatParameterMember> members;
        members.reserve(layout.members.size());
        for (const auto& member : layout.members) {
            const auto property = std::ranges::find(shader.properties, member.name,
                                                    &shader_authoring::AshaderPropertyDecl::name);
            if (property == shader.properties.end() || !matchesType(property->type, member)) {
                return std::unexpected{
                    layoutError(member.name, "missing property or scalar/width/size mismatch")};
            }
            members.push_back(
                {.propertyId = member.name, .type = property->type, .byteOffset = member.offset});
        }
        auto packed =
            material_instance::packAmatParameters(document, shader, members, layout.size, options);
        if (!packed) {
            return std::unexpected{std::move(packed.error())};
        }
        return ReflectedMaterialParameters{.layout = layout, .parameters = std::move(*packed)};
    }
} // namespace asharia::shader_material
