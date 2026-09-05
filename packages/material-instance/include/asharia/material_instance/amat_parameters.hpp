#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/material_instance/amat_resolver.hpp"

namespace asharia::material_instance {

    inline constexpr std::uint32_t kMaxAmatParameterBytes = 64U * 1024U;
    inline constexpr std::uint32_t kMaxAmatParameters = 256;

    enum class AmatParameterError {
        InvalidInput = 1,
        InvalidLayout,
        InvalidValue,
        UnsupportedType,
        BudgetExceeded,
    };

    struct AmatParameterMember {
        std::string propertyId;
        shader_authoring::AshaderPropertyType type{shader_authoring::AshaderPropertyType::Float};
        std::uint32_t byteOffset{};
    };

    struct AmatParameterBlock {
        std::vector<std::byte> bytes;
        std::vector<AmatDiagnostic> diagnostics;
    };

    // Numeric-only CPU packing. Layout offsets are supplied, never inferred. The caller must
    // verify layout against the compiled shader before GPU use; descriptor signature hashes
    // alone do not prove member-layout compatibility. Any failure returns no partial block.
    [[nodiscard]] Result<AmatParameterBlock>
    packAmatParameters(const AmatDocument& document,
                       const shader_authoring::AshaderDocument& shader,
                       std::span<const AmatParameterMember> members, std::uint32_t byteSize,
                       const AmatResolveOptions& options = {});

} // namespace asharia::material_instance
