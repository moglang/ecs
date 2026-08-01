#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t EcsWorldHandle;
typedef uint64_t EcsQueryHandle;
typedef uint64_t EcsEntityId;
typedef uint32_t EcsComponentTypeId;

typedef enum EcsErrorCodeC {
    ECS_OK = 0,
    ECS_INVALID_WORLD = 1,
    ECS_INVALID_QUERY = 2,
    ECS_INVALID_ENTITY = 3,
    ECS_STALE_ENTITY = 4,
    ECS_INVALID_COMPONENT_TYPE = 5,
    ECS_COMPONENT_ALREADY_EXISTS = 6,
    ECS_COMPONENT_NOT_FOUND = 7,
    ECS_INVALID_COMPONENT_SIZE = 8,
    ECS_INVALID_ALIGNMENT = 9,
    ECS_QUERY_INVALIDATED = 10,
    ECS_INVALID_ARGUMENT = 11,
    ECS_OUT_OF_MEMORY = 12,
    ECS_INTERNAL_ERROR = 13,
    ECS_COMPONENT_TYPE_CONFLICT = 14,
} EcsErrorCodeC;

typedef struct EcsResult {
    EcsErrorCodeC code;
} EcsResult;

EcsResult ecs_world_create(EcsWorldHandle* out_world);
EcsResult ecs_world_destroy(EcsWorldHandle world);
EcsResult ecs_entity_create(EcsWorldHandle world, EcsEntityId* out_entity);
EcsResult ecs_entity_destroy(EcsWorldHandle world, EcsEntityId entity);
EcsResult ecs_entity_is_alive(EcsWorldHandle world, EcsEntityId entity,
                              bool* out_is_alive);

EcsResult ecs_component_register(EcsWorldHandle world, const char* name,
                                 uint32_t size, uint32_t alignment,
                                 EcsComponentTypeId* out_component_type);
EcsResult ecs_component_add(EcsWorldHandle world, EcsEntityId entity,
                            EcsComponentTypeId component_type, const void* data,
                            uint32_t data_size);
EcsResult ecs_component_remove(EcsWorldHandle world, EcsEntityId entity,
                               EcsComponentTypeId component_type);
EcsResult ecs_component_has(EcsWorldHandle world, EcsEntityId entity,
                            EcsComponentTypeId component_type,
                            bool* out_has_component);
EcsResult ecs_component_read(EcsWorldHandle world, EcsEntityId entity,
                             EcsComponentTypeId component_type, void* out_data,
                             uint32_t out_data_size);
EcsResult ecs_component_write(EcsWorldHandle world, EcsEntityId entity,
                              EcsComponentTypeId component_type,
                              const void* data, uint32_t data_size);

EcsResult ecs_query_create(EcsWorldHandle world,
                           const EcsComponentTypeId* required_types,
                           uint32_t required_type_count,
                           EcsQueryHandle* out_query);
EcsResult ecs_query_next(EcsQueryHandle query, bool* out_has_value);
EcsResult ecs_query_entity(EcsQueryHandle query, EcsEntityId* out_entity);
EcsResult ecs_query_component_read(EcsQueryHandle query,
                                   EcsComponentTypeId component_type,
                                   void* out_data, uint32_t out_data_size);
EcsResult ecs_query_component_write(EcsQueryHandle query,
                                    EcsComponentTypeId component_type,
                                    const void* data, uint32_t data_size);
EcsResult ecs_query_destroy(EcsQueryHandle query);

const char* ecs_last_error_message(void);

#ifdef __cplusplus
}
#endif
