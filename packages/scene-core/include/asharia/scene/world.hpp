#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"
#include "asharia/scene/entity_id.hpp"
#include "asharia/scene/transform.hpp"

namespace asharia {

    enum class SceneErrorCode {
        InvalidEntity = 1,
        EntityCapacityExceeded = 2,
    };

    class World {
    public:
        [[nodiscard]] Result<EntityId> createEntity(std::string_view name = {});
        [[nodiscard]] VoidResult destroyEntity(EntityId entity);

        [[nodiscard]] bool isAlive(EntityId entity) const noexcept;
        [[nodiscard]] std::size_t aliveEntityCount() const noexcept;

        [[nodiscard]] std::string_view entityName(EntityId entity) const noexcept;
        [[nodiscard]] VoidResult setEntityName(EntityId entity, std::string_view name);

        // Immediate access only; do not cache across World mutation.
        [[nodiscard]] TransformComponent* tryGetTransform(EntityId entity) noexcept;
        [[nodiscard]] const TransformComponent* tryGetTransform(EntityId entity) const noexcept;
        [[nodiscard]] VoidResult setTransform(EntityId entity, const TransformComponent& transform);

    private:
        struct EntitySlot {
            std::uint32_t generation{1};
            bool alive{};
            std::string name;
            TransformComponent transform{};
        };

        [[nodiscard]] EntitySlot* findAliveSlot(EntityId entity) noexcept;
        [[nodiscard]] const EntitySlot* findAliveSlot(EntityId entity) const noexcept;

        std::vector<EntitySlot> slots_;
        std::vector<std::uint32_t> freeSlots_;
        std::size_t aliveEntityCount_{};
    };

} // namespace asharia
