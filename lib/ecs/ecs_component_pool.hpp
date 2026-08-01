#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "ecs/ecs_component_type.hpp"
#include "ecs/ecs_entity.hpp"

namespace mog::ecs {

class ComponentPool {
public:
    explicit ComponentPool(EcsComponentTypeInfo type_info)
        : type_info_(std::move(type_info)) {}

    EcsErrorCode add(EcsEntityId entity, const void* component_data,
                     std::uint32_t data_size);
    bool remove(EcsEntityId entity) noexcept;
    bool contains(EcsEntityId entity) const noexcept;

    EcsErrorCode read(EcsEntityId entity, void* out_data,
                      std::uint32_t out_data_size) const noexcept;
    EcsErrorCode write(EcsEntityId entity, const void* data,
                       std::uint32_t data_size) noexcept;

    const EcsComponentTypeInfo& type_info() const noexcept { return type_info_; }
    std::size_t size() const noexcept { return dense_entities_.size(); }
    EcsEntityId entity_at(std::size_t dense_index) const noexcept {
        return dense_entities_[dense_index];
    }

private:
    static constexpr std::uint32_t kInvalidDenseIndex =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t dense_index(EcsEntityId entity) const noexcept;

    EcsComponentTypeInfo type_info_;
    std::vector<EcsEntityId> dense_entities_;
    std::vector<std::byte> dense_components_;
    std::vector<std::uint32_t> sparse_;
};

}  // namespace mog::ecs
