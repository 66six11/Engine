#include "asharia/scene/world.hpp"

#include <limits>
#include <string>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia {
    namespace {

        [[nodiscard]] std::string entityText(EntityId entity) {
            return "entity(" + std::to_string(entity.index) + "," +
                   std::to_string(entity.generation) + ")";
        }

        [[nodiscard]] Error makeSceneError(SceneErrorCode code, std::string message) {
            return Error{ErrorDomain::Scene, static_cast<int>(code), std::move(message)};
        }

        [[nodiscard]] Error makeInvalidEntityError(EntityId entity, std::string_view operation) {
            std::string message{"Scene operation rejected invalid entity"};
            message += " [operation=";
            message += operation;
            message += "; entity=";
            message += entityText(entity);
            message += "]";
            return makeSceneError(SceneErrorCode::InvalidEntity, std::move(message));
        }

        [[nodiscard]] std::uint32_t nextGeneration(std::uint32_t generation) noexcept {
            ++generation;
            if (generation == 0U) {
                generation = 1U;
            }
            return generation;
        }

    } // namespace

    Result<EntityId> World::createEntity(std::string_view name) {
        std::uint32_t slotIndex = 0U;
        if (!freeSlots_.empty()) {
            slotIndex = freeSlots_.back();
            freeSlots_.pop_back();
        } else {
            if (slots_.size() >=
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                return std::unexpected(
                    makeSceneError(SceneErrorCode::EntityCapacityExceeded,
                                   "Scene operation rejected entity creation "
                                   "[operation=createEntity; reason=capacity exceeded]"));
            }
            slotIndex = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back(EntitySlot{});
        }

        EntitySlot& slot = slots_[slotIndex];
        slot.alive = true;
        slot.name = std::string{name};
        slot.transform = TransformComponent{};
        ++aliveEntityCount_;

        return EntityId{.index = slotIndex + 1U, .generation = slot.generation};
    }

    VoidResult World::destroyEntity(EntityId entity) {
        EntitySlot* slot = findAliveSlot(entity);
        if (slot == nullptr) {
            return std::unexpected(makeInvalidEntityError(entity, "destroyEntity"));
        }

        slot->alive = false;
        slot->name.clear();
        slot->transform = TransformComponent{};
        slot->generation = nextGeneration(slot->generation);
        freeSlots_.push_back(entity.index - 1U);
        --aliveEntityCount_;

        return {};
    }

    bool World::isAlive(EntityId entity) const noexcept {
        return findAliveSlot(entity) != nullptr;
    }

    std::size_t World::aliveEntityCount() const noexcept {
        return aliveEntityCount_;
    }

    std::string_view World::entityName(EntityId entity) const noexcept {
        const EntitySlot* slot = findAliveSlot(entity);
        if (slot == nullptr) {
            return {};
        }
        return slot->name;
    }

    VoidResult World::setEntityName(EntityId entity, std::string_view name) {
        EntitySlot* slot = findAliveSlot(entity);
        if (slot == nullptr) {
            return std::unexpected(makeInvalidEntityError(entity, "setEntityName"));
        }

        slot->name = std::string{name};
        return {};
    }

    TransformComponent* World::tryGetTransform(EntityId entity) noexcept {
        EntitySlot* slot = findAliveSlot(entity);
        if (slot == nullptr) {
            return nullptr;
        }
        return &slot->transform;
    }

    const TransformComponent* World::tryGetTransform(EntityId entity) const noexcept {
        const EntitySlot* slot = findAliveSlot(entity);
        if (slot == nullptr) {
            return nullptr;
        }
        return &slot->transform;
    }

    VoidResult World::setTransform(EntityId entity, const TransformComponent& transform) {
        EntitySlot* slot = findAliveSlot(entity);
        if (slot == nullptr) {
            return std::unexpected(makeInvalidEntityError(entity, "setTransform"));
        }

        slot->transform = transform;
        return {};
    }

    World::EntitySlot* World::findAliveSlot(EntityId entity) noexcept {
        if (!isValid(entity)) {
            return nullptr;
        }

        const std::uint32_t slotIndex = entity.index - 1U;
        if (slotIndex >= slots_.size()) {
            return nullptr;
        }

        EntitySlot& slot = slots_[slotIndex];
        if (!slot.alive || slot.generation != entity.generation) {
            return nullptr;
        }

        return &slot;
    }

    const World::EntitySlot* World::findAliveSlot(EntityId entity) const noexcept {
        if (!isValid(entity)) {
            return nullptr;
        }

        const std::uint32_t slotIndex = entity.index - 1U;
        if (slotIndex >= slots_.size()) {
            return nullptr;
        }

        const EntitySlot& slot = slots_[slotIndex];
        if (!slot.alive || slot.generation != entity.generation) {
            return nullptr;
        }

        return &slot;
    }

} // namespace asharia
