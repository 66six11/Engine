#include <cstdlib>
#include <iostream>
#include <string_view>

#include "asharia/scene/world.hpp"

namespace {

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    [[nodiscard]] bool smokeEntityLifetime() {
        asharia::World world;
        if (world.aliveEntityCount() != 0U || world.isAlive(asharia::kInvalidEntityId)) {
            std::cerr << "Empty world reported live entities.\n";
            return false;
        }

        auto first = world.createEntity("Cube");
        if (!first || !asharia::isValid(*first) || !world.isAlive(*first) ||
            world.aliveEntityCount() != 1U || world.entityName(*first) != "Cube") {
            std::cerr << "World did not create a live entity.\n";
            return false;
        }

        if (auto renamed = world.setEntityName(*first, "Moved Cube");
            !renamed || world.entityName(*first) != "Moved Cube") {
            std::cerr << "World did not update entity name.\n";
            return false;
        }

        if (auto destroyed = world.destroyEntity(*first); !destroyed) {
            std::cerr << destroyed.error().message << '\n';
            return false;
        }
        if (world.isAlive(*first) || world.tryGetTransform(*first) != nullptr ||
            world.aliveEntityCount() != 0U) {
            std::cerr << "Destroyed entity stayed alive.\n";
            return false;
        }

        auto staleDestroy = world.destroyEntity(*first);
        if (staleDestroy || !contains(staleDestroy.error().message, "destroyEntity")) {
            std::cerr << "World accepted a stale entity destroy.\n";
            return false;
        }

        auto second = world.createEntity("Sphere");
        if (!second || second->index != first->index || second->generation == first->generation ||
            world.isAlive(*first) || !world.isAlive(*second)) {
            std::cerr << "World did not invalidate stale id when reusing a slot.\n";
            return false;
        }

        return true;
    }

    [[nodiscard]] bool smokeTransformIdentity() {
        asharia::World world;
        auto entity = world.createEntity("Transform");
        if (!entity) {
            std::cerr << entity.error().message << '\n';
            return false;
        }

        const asharia::TransformComponent* initial = world.tryGetTransform(*entity);
        if (initial == nullptr || initial->position != asharia::Vec3{} ||
            initial->rotation != asharia::Quat{} ||
            initial->scale != asharia::Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F}) {
            std::cerr << "World did not assign the default transform.\n";
            return false;
        }

        const asharia::EntityId entityBeforeMove = *entity;
        const asharia::TransformComponent moved{
            .position = {.x = 3.0F, .y = 4.0F, .z = 5.0F},
            .rotation = {.x = 0.0F, .y = 0.707F, .z = 0.0F, .w = 0.707F},
            .scale = {.x = 2.0F, .y = 2.0F, .z = 2.0F},
        };
        if (auto updated = world.setTransform(*entity, moved); !updated) {
            std::cerr << updated.error().message << '\n';
            return false;
        }

        const asharia::TransformComponent* stored = world.tryGetTransform(*entity);
        if (*entity != entityBeforeMove || stored == nullptr || *stored != moved) {
            std::cerr << "Moving an entity changed its identity or lost transform state.\n";
            return false;
        }

        if (auto destroyed = world.destroyEntity(*entity); !destroyed) {
            std::cerr << destroyed.error().message << '\n';
            return false;
        }

        auto staleTransform = world.setTransform(*entity, moved);
        if (staleTransform || !contains(staleTransform.error().message, "setTransform")) {
            std::cerr << "World accepted transform mutation through a stale id.\n";
            return false;
        }

        return true;
    }

} // namespace

int main() {
    if (!smokeEntityLifetime() || !smokeTransformIdentity()) {
        return EXIT_FAILURE;
    }

    std::cout << "Scene core smoke tests passed.\n";
    return EXIT_SUCCESS;
}
