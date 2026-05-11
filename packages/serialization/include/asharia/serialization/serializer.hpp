#pragma once

#include "asharia/core/result.hpp"
#include "asharia/reflection/type_registry.hpp"
#include "asharia/serialization/archive_value.hpp"

namespace asharia::serialization {

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

} // namespace asharia::serialization
