#include "ecs/ecs_world.hpp"

#include <string>
#include <memory>

namespace mog::ecs {
namespace {

bool is_power_of_two(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

}  // namespace

EcsEntityId EcsWorld::create_entity() {
    const EcsEntityId entity = entities_.create();
    ++structural_version_;
    return entity;
}

bool EcsWorld::destroy_entity(EcsEntityId entity) {
    return destroy_entity_checked(entity) == EcsErrorCode::Ok;
}

EcsErrorCode EcsWorld::destroy_entity_checked(EcsEntityId entity) {
    const EcsErrorCode validation = validate_entity(entity);
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }

    for (auto& entry : pools_) {
        entry.second->remove(entity);
    }
    if (!entities_.destroy(entity)) {
        return EcsErrorCode::InternalError;
    }
    ++structural_version_;
    return EcsErrorCode::Ok;
}

ComponentRegistrationResult EcsWorld::register_component(
    std::string_view name, std::uint32_t size, std::uint32_t alignment) {
    if (name.empty()) {
        return {EcsErrorCode::InvalidArgument, 0};
    }
    if (size == 0) {
        return {EcsErrorCode::InvalidComponentSize, 0};
    }
    if (!is_power_of_two(alignment)) {
        return {EcsErrorCode::InvalidAlignment, 0};
    }

    const std::string owned_name(name);
    const auto existing_name = component_names_.find(owned_name);
    if (existing_name != component_names_.end()) {
        const EcsComponentTypeInfo& existing =
            component_types_.at(existing_name->second);
        if (existing.size == size && existing.alignment == alignment) {
            return {EcsErrorCode::Ok, existing.id};
        }
        return {EcsErrorCode::ComponentTypeConflict, 0};
    }

    // Zero is reserved as an invalid component type ID. Reaching it here
    // means the 32-bit ID space wrapped after being exhausted.
    if (next_component_type_id_ == 0) {
        return {EcsErrorCode::InternalError, 0};
    }

    const EcsComponentTypeId id = next_component_type_id_;
    EcsComponentTypeInfo info{id, owned_name, size, alignment};
    auto pool = std::make_unique<ComponentPool>(info);
    component_types_.emplace(id, info);
    try {
        component_names_.emplace(info.name, id);
        pools_.emplace(id, std::move(pool));
    } catch (...) {
        pools_.erase(id);
        component_names_.erase(info.name);
        component_types_.erase(id);
        throw;
    }
    ++next_component_type_id_;
    ++structural_version_;
    return {EcsErrorCode::Ok, id};
}

const EcsComponentTypeInfo* EcsWorld::find_component_type(
    EcsComponentTypeId component_type) const noexcept {
    const auto found = component_types_.find(component_type);
    return found == component_types_.end() ? nullptr : &found->second;
}

const EcsComponentTypeInfo* EcsWorld::find_component_type(
    std::string_view name) const {
    const auto found = component_names_.find(std::string(name));
    return found == component_names_.end()
               ? nullptr
               : find_component_type(found->second);
}

EcsErrorCode EcsWorld::add_component(EcsEntityId entity,
                                     EcsComponentTypeId component_type,
                                     const void* data,
                                     std::uint32_t data_size) {
    const EcsErrorCode validation = validate_entity(entity);
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }
    ComponentPool* pool = find_pool(component_type);
    if (pool == nullptr) {
        return EcsErrorCode::InvalidComponentType;
    }
    const EcsErrorCode result = pool->add(entity, data, data_size);
    if (result == EcsErrorCode::Ok) {
        ++structural_version_;
    }
    return result;
}

EcsErrorCode EcsWorld::remove_component(EcsEntityId entity,
                                        EcsComponentTypeId component_type) {
    const EcsErrorCode validation = validate_entity(entity);
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }
    ComponentPool* pool = find_pool(component_type);
    if (pool == nullptr) {
        return EcsErrorCode::InvalidComponentType;
    }
    if (!pool->remove(entity)) {
        return EcsErrorCode::ComponentNotFound;
    }
    ++structural_version_;
    return EcsErrorCode::Ok;
}

EcsErrorCode EcsWorld::has_component(
    EcsEntityId entity, EcsComponentTypeId component_type,
    bool& out_has_component) const noexcept {
    const EcsErrorCode validation = validate_entity(entity);
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }
    const ComponentPool* pool = find_pool(component_type);
    if (pool == nullptr) {
        return EcsErrorCode::InvalidComponentType;
    }
    out_has_component = pool->contains(entity);
    return EcsErrorCode::Ok;
}

EcsErrorCode EcsWorld::read_component(
    EcsEntityId entity, EcsComponentTypeId component_type, void* out_data,
    std::uint32_t out_data_size) const noexcept {
    const EcsErrorCode validation = validate_entity(entity);
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }
    const ComponentPool* pool = find_pool(component_type);
    return pool == nullptr
               ? EcsErrorCode::InvalidComponentType
               : pool->read(entity, out_data, out_data_size);
}

EcsErrorCode EcsWorld::write_component(
    EcsEntityId entity, EcsComponentTypeId component_type, const void* data,
    std::uint32_t data_size) noexcept {
    const EcsErrorCode validation = validate_entity(entity);
    if (validation != EcsErrorCode::Ok) {
        return validation;
    }
    ComponentPool* pool = find_pool(component_type);
    return pool == nullptr
               ? EcsErrorCode::InvalidComponentType
               : pool->write(entity, data, data_size);
}

ComponentPool* EcsWorld::find_pool(
    EcsComponentTypeId component_type) noexcept {
    const auto found = pools_.find(component_type);
    return found == pools_.end() ? nullptr : found->second.get();
}

const ComponentPool* EcsWorld::find_pool(
    EcsComponentTypeId component_type) const noexcept {
    const auto found = pools_.find(component_type);
    return found == pools_.end() ? nullptr : found->second.get();
}

EcsErrorCode EcsWorld::validate_entity(EcsEntityId entity) const noexcept {
    switch (entities_.validate(entity)) {
        case EntityValidation::Alive:
            return EcsErrorCode::Ok;
        case EntityValidation::Stale:
            return EcsErrorCode::StaleEntity;
        case EntityValidation::Invalid:
            return EcsErrorCode::InvalidEntity;
    }
    return EcsErrorCode::InternalError;
}

}  // namespace mog::ecs
