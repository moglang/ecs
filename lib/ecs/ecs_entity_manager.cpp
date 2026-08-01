#include "ecs/ecs_entity_manager.hpp"

#include <limits>
#include <stdexcept>

namespace mog::ecs {
namespace {

constexpr std::uint32_t kFirstGeneration = 1;

std::uint32_t next_generation(std::uint32_t generation) noexcept {
    ++generation;
    // Generation zero is reserved, which also keeps every valid entity ID
    // distinct from the conventional null/invalid value zero.
    return generation == 0 ? kFirstGeneration : generation;
}

}  // namespace

EcsEntityId EntityManager::create() {
    std::uint32_t index = 0;

    if (!free_indices_.empty()) {
        index = free_indices_.back();
        free_indices_.pop_back();
        generations_[index] = next_generation(generations_[index]);
        alive_[index] = true;
    } else {
        if (generations_.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            throw std::length_error("ECS entity index space is exhausted");
        }

        index = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(kFirstGeneration);
        try {
            alive_.push_back(true);
        } catch (...) {
            generations_.pop_back();
            throw;
        }
    }

    ++alive_count_;
    return pack_entity(index, generations_[index]);
}

bool EntityManager::destroy(EcsEntityId entity) {
    if (!is_alive(entity)) {
        return false;
    }

    const std::uint32_t index = entity_index(entity);
    free_indices_.push_back(index);
    alive_[index] = false;
    --alive_count_;
    return true;
}

bool EntityManager::is_alive(EcsEntityId entity) const noexcept {
    return validate(entity) == EntityValidation::Alive;
}

EntityValidation EntityManager::validate(EcsEntityId entity) const noexcept {
    const std::uint32_t index = entity_index(entity);
    if (index >= generations_.size()) {
        return EntityValidation::Invalid;
    }
    if (generations_[index] != entity_generation(entity)) {
        return EntityValidation::Stale;
    }
    return alive_[index] ? EntityValidation::Alive : EntityValidation::Invalid;
}

}  // namespace mog::ecs
