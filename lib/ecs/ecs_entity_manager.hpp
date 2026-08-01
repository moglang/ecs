#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ecs/ecs_entity.hpp"

namespace mog::ecs {

enum class EntityValidation {
    Alive,
    Invalid,
    Stale,
};

class EntityManager {
public:
    EcsEntityId create();
    bool destroy(EcsEntityId entity);
    bool is_alive(EcsEntityId entity) const noexcept;
    EntityValidation validate(EcsEntityId entity) const noexcept;

    std::size_t alive_count() const noexcept { return alive_count_; }

private:
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint32_t> free_indices_;
    std::vector<bool> alive_;
    std::size_t alive_count_ = 0;
};

}  // namespace mog::ecs
