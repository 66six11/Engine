#pragma once

#include "asharia/material_instance/amat_parameters.hpp"
#include "asharia/shader_slang/reflection.hpp"

namespace asharia::shader_material {

    struct ReflectedMaterialParameters {
        ShaderParameterBlockReflection layout;
        material_instance::AmatParameterBlock parameters;
    };

    // Checks an explicitly selected constant-buffer binding. Retain layout with the compiled
    // shader product identity; descriptor signature hashes alone do not identify member layout.
    [[nodiscard]] Result<ReflectedMaterialParameters>
    packReflectedMaterialParameters(const material_instance::AmatDocument& document,
                                    const shader_authoring::AshaderDocument& shader,
                                    const ShaderDescriptorBindingReflection& binding,
                                    const material_instance::AmatResolveOptions& options = {});

} // namespace asharia::shader_material
