#include "ecs/ecs_query.hpp"

#include <algorithm>
#include <utility>

#include "ecs/ecs_component_pool.hpp"
#include "ecs/ecs_world.hpp"

namespace mog::ecs {

EcsErrorCode EcsQuery::create(
    EcsWorld& world, const EcsComponentTypeId* required_types,
    std::uint32_t required_type_count, std::unique_ptr<EcsQuery>& out_query) {
    out_query.reset();
    if (required_types == nullptr || required_type_count == 0) {
        return EcsErrorCode::InvalidArgument;
    }

    std::vector<EcsComponentTypeId> types;
    std::vector<ComponentPool*> pools;
    types.reserve(required_type_count);
    pools.reserve(required_type_count);
    ComponentPool* driver = nullptr;

    for (std::uint32_t index = 0; index < required_type_count; ++index) {
        const EcsComponentTypeId type = required_types[index];
        if (std::find(types.begin(), types.end(), type) != types.end()) {
            return EcsErrorCode::InvalidArgument;
        }
        ComponentPool* pool = world.find_pool(type);
        if (pool == nullptr) {
            return EcsErrorCode::InvalidComponentType;
        }
        types.push_back(type);
        pools.push_back(pool);
        if (driver == nullptr || pool->size() < driver->size()) {
            driver = pool;
        }
    }

    out_query = std::unique_ptr<EcsQuery>(
        new EcsQuery(world, std::move(types), std::move(pools), driver));
    return EcsErrorCode::Ok;
}

EcsQuery::EcsQuery(EcsWorld& world,
                   std::vector<EcsComponentTypeId> required_types,
                   std::vector<ComponentPool*> pools,
                   ComponentPool* driver) noexcept
    : world_(&world),
      required_types_(std::move(required_types)),
      pools_(std::move(pools)),
      driver_(driver),
      structural_version_(world.structural_version()) {}

EcsErrorCode EcsQuery::next(bool& out_has_value) noexcept {
    out_has_value = false;
    has_current_ = false;
    const EcsErrorCode validation = validate_iteration();
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }

    while (cursor_ < driver_->size()) {
        const EcsEntityId candidate = driver_->entity_at(cursor_++);
        bool matches = true;
        for (const ComponentPool* pool : pools_) {
            if (!pool->contains(candidate)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            current_entity_ = candidate;
            has_current_ = true;
            out_has_value = true;
            break;
        }
    }
    return EcsErrorCode::Ok;
}

EcsErrorCode EcsQuery::entity(EcsEntityId& out_entity) const noexcept {
    const EcsErrorCode validation = validate_iteration();
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }
    if (!has_current_) {
        return EcsErrorCode::InvalidArgument;
    }
    out_entity = current_entity_;
    return EcsErrorCode::Ok;
}

EcsErrorCode EcsQuery::read_component(
    EcsComponentTypeId component_type, void* out_data,
    std::uint32_t out_data_size) const noexcept {
    const EcsErrorCode validation = validate_iteration();
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }
    if (!has_current_) {
        return EcsErrorCode::InvalidArgument;
    }
    if (!contains_required_type(component_type)) {
        return EcsErrorCode::InvalidComponentType;
    }
    return world_->read_component(current_entity_, component_type, out_data,
                                  out_data_size);
}

EcsErrorCode EcsQuery::write_component(EcsComponentTypeId component_type,
                                       const void* data,
                                       std::uint32_t data_size) noexcept {
    const EcsErrorCode validation = validate_iteration();
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }
    if (!has_current_) {
        return EcsErrorCode::InvalidArgument;
    }
    if (!contains_required_type(component_type)) {
        return EcsErrorCode::InvalidComponentType;
    }
    return world_->write_component(current_entity_, component_type, data,
                                   data_size);
}

EcsErrorCode EcsQuery::validate_iteration() const noexcept {
    if (world_ == nullptr || driver_ == nullptr) {
        return EcsErrorCode::InvalidQuery;
    }
    return world_->structural_version() == structural_version_
               ? EcsErrorCode::Ok
               : EcsErrorCode::QueryInvalidated;
}

bool EcsQuery::contains_required_type(
    EcsComponentTypeId component_type) const noexcept {
    return std::find(required_types_.begin(), required_types_.end(),
                     component_type) != required_types_.end();
}

}  // namespace mog::ecs
