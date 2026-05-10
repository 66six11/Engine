#pragma once

#include "vke/core/result.hpp"
#include "vke/reflection/type_registry.hpp"
#include "vke/serialization/archive_value.hpp"

namespace vke::serialization {

    struct SerializationPolicy {
        bool includeTypeHeader{true};
        bool allowUnknownFields{true};
    };

    [[nodiscard]] Result<ArchiveValue>
    serializeObject(const reflection::TypeRegistry& registry, reflection::TypeId type,
                    const void* object, const SerializationPolicy& policy = {});

    [[nodiscard]] VoidResult
    deserializeObject(const reflection::TypeRegistry& registry, reflection::TypeId type,
                      const ArchiveValue& value, void* object,
                      const SerializationPolicy& policy = {});

} // namespace vke::serialization
