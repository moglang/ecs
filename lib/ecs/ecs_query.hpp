#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "ecs/ecs_component_type.hpp"
#include "ecs/ecs_entity.hpp"
#include "ecs/ecs_error.hpp"

namespace mog::ecs {

class ComponentPool;
class EcsWorld;

class EcsQuery {
public:
    static EcsErrorCode create(
        EcsWorld& world, const EcsComponentTypeId* required_types,
        std::uint32_t required_type_count,
        std::unique_ptr<EcsQuery>& out_query);

    EcsErrorCode next(bool& out_has_value) noexcept;
    EcsErrorCode entity(EcsEntityId& out_entity) const noexcept;
    EcsErrorCode read_component(EcsComponentTypeId component_type,
                                void* out_data,
                                std::uint32_t out_data_size) const noexcept;
    EcsErrorCode write_component(EcsComponentTypeId component_type,
                                 const void* data,
                                 std::uint32_t data_size) noexcept;

private:
    EcsQuery(EcsWorld& world, std::vector<EcsComponentTypeId> required_types,
             std::vector<ComponentPool*> pools, ComponentPool* driver) noexcept;

    EcsErrorCode validate_iteration() const noexcept;
    bool contains_required_type(
        EcsComponentTypeId component_type) const noexcept;

    EcsWorld* world_;
    std::vector<EcsComponentTypeId> required_types_;
    std::vector<ComponentPool*> pools_;
    ComponentPool* driver_;
    std::uint64_t structural_version_;
    std::size_t cursor_ = 0;
    EcsEntityId current_entity_ = 0;
    bool has_current_ = false;
};

}  // namespace mog::ecs
