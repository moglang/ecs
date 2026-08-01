#pragma once

#include <cstdint>

namespace mog::ecs {

using EcsEntityId = std::uint64_t;

constexpr EcsEntityId pack_entity(std::uint32_t index,
                                  std::uint32_t generation) noexcept {
    return (static_cast<EcsEntityId>(generation) << 32U) |
           static_cast<EcsEntityId>(index);
}

constexpr std::uint32_t entity_index(EcsEntityId entity) noexcept {
    return static_cast<std::uint32_t>(entity);
}

constexpr std::uint32_t entity_generation(EcsEntityId entity) noexcept {
    return static_cast<std::uint32_t>(entity >> 32U);
}

}  // namespace mog::ecs
