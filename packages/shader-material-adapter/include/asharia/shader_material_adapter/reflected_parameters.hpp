#pragma once

#include "asharia/material_instance/mat_parameters.hpp"
#include "asharia/shader_slang/reflection.hpp"

namespace asharia::shader_material {

    struct ReflectedMaterialParameters {
        ShaderParameterBlockReflection layout;
        material_instance::MatParameterBlock parameters;
    };

    // Checks an explicitly selected constant-buffer binding. Retain layout with the compiled
    // shader product identity; descriptor signature hashes alone do not identify member layout.
    [[nodiscard]] Result<ReflectedMaterialParameters>
    packReflectedMaterialParameters(const material_instance::MatDocument& document,
                                    const shader_authoring::AshaderDocument& shader,
                                    const ShaderDescriptorBindingReflection& binding,
                                    const material_instance::MatResolveOptions& options = {});

} // namespace asharia::shader_material
