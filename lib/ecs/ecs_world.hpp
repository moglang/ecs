#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <memory>

#include "ecs/ecs_component_pool.hpp"
#include "ecs/ecs_component_type.hpp"
#include "ecs/ecs_entity_manager.hpp"

namespace mog::ecs {

class EcsWorld {
public:
    EcsEntityId create_entity();
    bool destroy_entity(EcsEntityId entity);
    EcsErrorCode destroy_entity_checked(EcsEntityId entity);
    bool is_alive(EcsEntityId entity) const noexcept {
        return entities_.is_alive(entity);
    }
    std::size_t alive_count() const noexcept { return entities_.alive_count(); }

    ComponentRegistrationResult register_component(std::string_view name,
                                                   std::uint32_t size,
                                                   std::uint32_t alignment);

    const EcsComponentTypeInfo* find_component_type(
        EcsComponentTypeId component_type) const noexcept;
    const EcsComponentTypeInfo* find_component_type(
        std::string_view name) const;
    std::size_t component_type_count() const noexcept {
        return component_types_.size();
    }

    EcsErrorCode add_component(EcsEntityId entity,
                               EcsComponentTypeId component_type,
                               const void* data, std::uint32_t data_size);
    EcsErrorCode remove_component(EcsEntityId entity,
                                  EcsComponentTypeId component_type);
    EcsErrorCode has_component(EcsEntityId entity,
                               EcsComponentTypeId component_type,
                               bool& out_has_component) const noexcept;
    EcsErrorCode read_component(EcsEntityId entity,
                                EcsComponentTypeId component_type,
                                void* out_data,
                                std::uint32_t out_data_size) const noexcept;
    EcsErrorCode write_component(EcsEntityId entity,
                                 EcsComponentTypeId component_type,
                                 const void* data,
                                 std::uint32_t data_size) noexcept;

    ComponentPool* find_pool(EcsComponentTypeId component_type) noexcept;
    const ComponentPool* find_pool(
        EcsComponentTypeId component_type) const noexcept;
    std::uint64_t structural_version() const noexcept {
        return structural_version_;
    }

private:
    EcsErrorCode validate_entity(EcsEntityId entity) const noexcept;

    EntityManager entities_;
    std::unordered_map<EcsComponentTypeId, EcsComponentTypeInfo>
        component_types_;
    std::unordered_map<std::string, EcsComponentTypeId> component_names_;
    std::unordered_map<EcsComponentTypeId, std::unique_ptr<ComponentPool>>
        pools_;
    EcsComponentTypeId next_component_type_id_ = 1;
    std::uint64_t structural_version_ = 0;
};

}  // namespace mog::ecs
