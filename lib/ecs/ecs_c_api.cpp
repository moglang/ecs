#include "ecs/ecs_c_api.h"

#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>

#include "ecs/ecs_handle_table.hpp"
#include "ecs/ecs_query.hpp"
#include "ecs/ecs_world.hpp"

namespace {

using mog::ecs::EcsErrorCode;
using mog::ecs::EcsQuery;
using mog::ecs::EcsWorld;
using mog::ecs::HandleTable;

struct QueryRecord {
    EcsWorldHandle world_handle;
    std::unique_ptr<EcsQuery> query;
};

HandleTable<EcsWorld> worlds;
HandleTable<QueryRecord> queries;
std::mutex handles_mutex;
thread_local std::string last_error;

EcsErrorCodeC to_c_error(EcsErrorCode code) noexcept {
    return static_cast<EcsErrorCodeC>(static_cast<std::uint32_t>(code));
}

const char* error_text(EcsErrorCode code) noexcept {
    switch (code) {
        case EcsErrorCode::Ok:
            return "";
        case EcsErrorCode::InvalidWorld:
            return "invalid or stale ECS world handle";
        case EcsErrorCode::InvalidQuery:
            return "invalid or stale ECS query handle";
        case EcsErrorCode::InvalidEntity:
            return "invalid or destroyed ECS entity";
        case EcsErrorCode::StaleEntity:
            return "stale ECS entity generation";
        case EcsErrorCode::InvalidComponentType:
            return "invalid ECS component type";
        case EcsErrorCode::ComponentAlreadyExists:
            return "entity already has this component";
        case EcsErrorCode::ComponentNotFound:
            return "entity does not have this component";
        case EcsErrorCode::InvalidComponentSize:
            return "component buffer size does not match its registered size";
        case EcsErrorCode::InvalidAlignment:
            return "component alignment must be a nonzero power of two";
        case EcsErrorCode::QueryInvalidated:
            return "query was invalidated by a structural world change";
        case EcsErrorCode::InvalidArgument:
            return "invalid ECS API argument";
        case EcsErrorCode::OutOfMemory:
            return "ECS allocation failed";
        case EcsErrorCode::InternalError:
            return "internal ECS error";
        case EcsErrorCode::ComponentTypeConflict:
            return "component name is registered with a different layout";
    }
    return "unknown ECS error";
}

EcsResult result(EcsErrorCode code) {
    last_error = error_text(code);
    return {to_c_error(code)};
}

template <typename Function>
EcsResult protect(Function function) noexcept {
    try {
        return result(function());
    } catch (const std::bad_alloc&) {
        return result(EcsErrorCode::OutOfMemory);
    } catch (const std::exception& error) {
        last_error = error.what();
        return {ECS_INTERNAL_ERROR};
    } catch (...) {
        return result(EcsErrorCode::InternalError);
    }
}

EcsWorld* find_world(EcsWorldHandle handle) noexcept {
    return worlds.get(handle);
}

QueryRecord* find_query(EcsQueryHandle handle) noexcept {
    return queries.get(handle);
}

}  // namespace

extern "C" EcsResult ecs_world_create(EcsWorldHandle* out_world) {
    return protect([&] {
        if (out_world == nullptr) {
            return EcsErrorCode::InvalidArgument;
        }
        std::lock_guard<std::mutex> lock(handles_mutex);
        *out_world = worlds.insert(std::make_unique<EcsWorld>());
        return EcsErrorCode::Ok;
    });
}

extern "C" EcsResult ecs_world_destroy(EcsWorldHandle world) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        if (find_world(world) == nullptr) {
            return EcsErrorCode::InvalidWorld;
        }
        queries.remove_if(
            [&](const QueryRecord& record) { return record.world_handle == world; });
        return worlds.remove(world) ? EcsErrorCode::Ok
                                    : EcsErrorCode::InternalError;
    });
}

extern "C" EcsResult ecs_entity_create(EcsWorldHandle world,
                                        EcsEntityId* out_entity) {
    return protect([&] {
        if (out_entity == nullptr) {
            return EcsErrorCode::InvalidArgument;
        }
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        if (value == nullptr) {
            return EcsErrorCode::InvalidWorld;
        }
        *out_entity = value->create_entity();
        return EcsErrorCode::Ok;
    });
}

extern "C" EcsResult ecs_entity_destroy(EcsWorldHandle world,
                                         EcsEntityId entity) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        return value == nullptr ? EcsErrorCode::InvalidWorld
                                : value->destroy_entity_checked(entity);
    });
}

extern "C" EcsResult ecs_entity_is_alive(EcsWorldHandle world,
                                          EcsEntityId entity,
                                          bool* out_is_alive) {
    return protect([&] {
        if (out_is_alive == nullptr) {
            return EcsErrorCode::InvalidArgument;
        }
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        if (value == nullptr) {
            return EcsErrorCode::InvalidWorld;
        }
        *out_is_alive = value->is_alive(entity);
        return EcsErrorCode::Ok;
    });
}

extern "C" EcsResult ecs_component_register(
    EcsWorldHandle world, const char* name, uint32_t size, uint32_t alignment,
    EcsComponentTypeId* out_component_type) {
    return protect([&] {
        if (name == nullptr || out_component_type == nullptr) {
            return EcsErrorCode::InvalidArgument;
        }
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        if (value == nullptr) {
            return EcsErrorCode::InvalidWorld;
        }
        const auto registration = value->register_component(name, size, alignment);
        if (registration.ok()) {
            *out_component_type = registration.component_type;
        }
        return registration.error;
    });
}

extern "C" EcsResult ecs_component_add(
    EcsWorldHandle world, EcsEntityId entity,
    EcsComponentTypeId component_type, const void* data, uint32_t data_size) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        return value == nullptr
                   ? EcsErrorCode::InvalidWorld
                   : value->add_component(entity, component_type, data, data_size);
    });
}

extern "C" EcsResult ecs_component_remove(
    EcsWorldHandle world, EcsEntityId entity,
    EcsComponentTypeId component_type) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        return value == nullptr
                   ? EcsErrorCode::InvalidWorld
                   : value->remove_component(entity, component_type);
    });
}

extern "C" EcsResult ecs_component_has(
    EcsWorldHandle world, EcsEntityId entity,
    EcsComponentTypeId component_type, bool* out_has_component) {
    return protect([&] {
        if (out_has_component == nullptr) {
            return EcsErrorCode::InvalidArgument;
        }
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        return value == nullptr
                   ? EcsErrorCode::InvalidWorld
                   : value->has_component(entity, component_type,
                                          *out_has_component);
    });
}

extern "C" EcsResult ecs_component_read(
    EcsWorldHandle world, EcsEntityId entity,
    EcsComponentTypeId component_type, void* out_data, uint32_t out_data_size) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        return value == nullptr
                   ? EcsErrorCode::InvalidWorld
                   : value->read_component(entity, component_type, out_data,
                                           out_data_size);
    });
}

extern "C" EcsResult ecs_component_write(
    EcsWorldHandle world, EcsEntityId entity,
    EcsComponentTypeId component_type, const void* data, uint32_t data_size) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        return value == nullptr
                   ? EcsErrorCode::InvalidWorld
                   : value->write_component(entity, component_type, data,
                                            data_size);
    });
}

extern "C" EcsResult ecs_query_create(
    EcsWorldHandle world, const EcsComponentTypeId* required_types,
    uint32_t required_type_count, EcsQueryHandle* out_query) {
    return protect([&] {
        if (out_query == nullptr) {
            return EcsErrorCode::InvalidArgument;
        }
        std::lock_guard<std::mutex> lock(handles_mutex);
        EcsWorld* value = find_world(world);
        if (value == nullptr) {
            return EcsErrorCode::InvalidWorld;
        }
        std::unique_ptr<EcsQuery> query;
        const EcsErrorCode created = EcsQuery::create(
            *value, required_types, required_type_count, query);
        if (created != EcsErrorCode::Ok) {
            return created;
        }
        auto record = std::make_unique<QueryRecord>();
        record->world_handle = world;
        record->query = std::move(query);
        *out_query = queries.insert(std::move(record));
        return EcsErrorCode::Ok;
    });
}

extern "C" EcsResult ecs_query_next(EcsQueryHandle query,
                                     bool* out_has_value) {
    return protect([&] {
        if (out_has_value == nullptr) {
            return EcsErrorCode::InvalidArgument;
        }
        std::lock_guard<std::mutex> lock(handles_mutex);
        QueryRecord* record = find_query(query);
        return record == nullptr ? EcsErrorCode::InvalidQuery
                                 : record->query->next(*out_has_value);
    });
}

extern "C" EcsResult ecs_query_entity(EcsQueryHandle query,
                                       EcsEntityId* out_entity) {
    return protect([&] {
        if (out_entity == nullptr) {
            return EcsErrorCode::InvalidArgument;
        }
        std::lock_guard<std::mutex> lock(handles_mutex);
        QueryRecord* record = find_query(query);
        return record == nullptr ? EcsErrorCode::InvalidQuery
                                 : record->query->entity(*out_entity);
    });
}

extern "C" EcsResult ecs_query_component_read(
    EcsQueryHandle query, EcsComponentTypeId component_type, void* out_data,
    uint32_t out_data_size) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        QueryRecord* record = find_query(query);
        return record == nullptr
                   ? EcsErrorCode::InvalidQuery
                   : record->query->read_component(component_type, out_data,
                                                   out_data_size);
    });
}

extern "C" EcsResult ecs_query_component_write(
    EcsQueryHandle query, EcsComponentTypeId component_type, const void* data,
    uint32_t data_size) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        QueryRecord* record = find_query(query);
        return record == nullptr
                   ? EcsErrorCode::InvalidQuery
                   : record->query->write_component(component_type, data,
                                                    data_size);
    });
}

extern "C" EcsResult ecs_query_destroy(EcsQueryHandle query) {
    return protect([&] {
        std::lock_guard<std::mutex> lock(handles_mutex);
        return queries.remove(query) ? EcsErrorCode::Ok
                                     : EcsErrorCode::InvalidQuery;
    });
}

extern "C" const char* ecs_last_error_message(void) {
    return last_error.c_str();
}
