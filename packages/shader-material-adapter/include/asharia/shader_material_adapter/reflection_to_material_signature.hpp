#pragma once

#include <cstdint>

#include "asharia/core/result.hpp"
#include "asharia/material/material_resource_signature.hpp"
#include "asharia/shader_slang/reflection.hpp"

namespace asharia::shader_material {

    struct ReflectionMaterialSignature {
        material::MaterialResourceSignature signature;
        std::uint64_t signatureHash{};
    };

    [[nodiscard]] Result<material::MaterialResourceSignature>
    makeMaterialResourceSignature(const ShaderResourceSignature& shaderSignature);

    [[nodiscard]] Result<ReflectionMaterialSignature>
    makeReflectionMaterialSignature(const ShaderResourceSignature& shaderSignature);

} // namespace asharia::shader_material
