#include "ecs/ecs_c_api.h"
#include "ecs/ecs_query.hpp"
#include "ecs/ecs_world.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Value {
    std::uint32_t x;
    std::uint32_t y;
};

template <typename Function>
double measure_ns(std::size_t operations, Function&& function) {
    const auto start = Clock::now();
    function();
    const auto elapsed = std::chrono::duration<double, std::nano>(Clock::now() - start);
    return elapsed.count() / static_cast<double>(operations);
}

void report(std::string_view boundary, std::size_t count,
            std::string_view operation, double nanoseconds) {
    std::cout << boundary << " n=" << count << ' ' << operation << ' '
              << nanoseconds << " ns/op\n";
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void benchmark_cpp(std::size_t count) {
    mog::ecs::EcsWorld world;
    const auto position = world.register_component("Position", sizeof(Value), alignof(Value));
    const auto velocity = world.register_component("Velocity", sizeof(Value), alignof(Value));
    require(position && velocity, "C++ component registration failed");
    std::vector<mog::ecs::EcsEntityId> entities;
    entities.reserve(count);
    report("cpp", count, "entity-create", measure_ns(count, [&] {
        for (std::size_t i = 0; i < count; ++i) entities.push_back(world.create_entity());
    }));
    const Value initial{1, 2};
    report("cpp", count, "component-add-two", measure_ns(count * 2, [&] {
        for (const auto entity : entities) {
            require(world.add_component(entity, position.component_type, &initial,
                                        sizeof(initial)) == mog::ecs::EcsErrorCode::Ok,
                    "C++ component add failed");
            require(world.add_component(entity, velocity.component_type, &initial,
                                        sizeof(initial)) == mog::ecs::EcsErrorCode::Ok,
                    "C++ component add failed");
        }
    }));
    std::uint64_t checksum = 0;
    report("cpp", count, "component-read-write", measure_ns(count * 2, [&] {
        for (const auto entity : entities) {
            Value value{};
            require(world.read_component(entity, position.component_type, &value,
                                         sizeof(value)) == mog::ecs::EcsErrorCode::Ok,
                    "C++ component read failed");
            ++value.x;
            checksum += value.x;
            require(world.write_component(entity, position.component_type, &value,
                                          sizeof(value)) == mog::ecs::EcsErrorCode::Ok,
                    "C++ component write failed");
        }
    }));
    std::unique_ptr<mog::ecs::EcsQuery> one_query;
    require(mog::ecs::EcsQuery::create(world, &position.component_type, 1,
                                       one_query) == mog::ecs::EcsErrorCode::Ok,
            "C++ one-component query creation failed");
    report("cpp", count, "query-one", measure_ns(count, [&] {
        bool has_value = false;
        while (one_query->next(has_value) == mog::ecs::EcsErrorCode::Ok && has_value) {
            mog::ecs::EcsEntityId entity = 0;
            require(one_query->entity(entity) == mog::ecs::EcsErrorCode::Ok,
                    "C++ query entity failed");
            checksum += entity != 0 ? 1U : 0U;
        }
    }));
    std::unique_ptr<mog::ecs::EcsQuery> query;
    const mog::ecs::EcsComponentTypeId required[] = {
        position.component_type, velocity.component_type};
    require(mog::ecs::EcsQuery::create(world, required, 2, query) ==
                mog::ecs::EcsErrorCode::Ok,
            "C++ query creation failed");
    report("cpp", count, "query-two-read-write", measure_ns(count, [&] {
        bool has_value = false;
        while (query->next(has_value) == mog::ecs::EcsErrorCode::Ok && has_value) {
            Value value{};
            require(query->read_component(position.component_type, &value, sizeof(value)) ==
                        mog::ecs::EcsErrorCode::Ok,
                    "C++ query read failed");
            ++value.x;
            checksum += value.x;
            require(query->write_component(position.component_type, &value, sizeof(value)) ==
                        mog::ecs::EcsErrorCode::Ok,
                    "C++ query write failed");
        }
    }));
    report("cpp", count, "component-remove", measure_ns(count, [&] {
        for (const auto entity : entities)
            require(world.remove_component(entity, velocity.component_type) ==
                        mog::ecs::EcsErrorCode::Ok,
                    "C++ component remove failed");
    }));
    report("cpp", count, "entity-destroy", measure_ns(count, [&] {
        for (const auto entity : entities) require(world.destroy_entity(entity), "C++ destroy failed");
    }));
    if (checksum == 0) std::abort();
}

void benchmark_c_api(std::size_t count) {
    EcsWorldHandle world = 0;
    require(ecs_world_create(&world).code == ECS_OK, "C API world creation failed");
    EcsComponentTypeId position = 0;
    EcsComponentTypeId velocity = 0;
    require(ecs_component_register(world, "Position", sizeof(Value), alignof(Value),
                                   &position).code == ECS_OK,
            "C API component registration failed");
    require(ecs_component_register(world, "Velocity", sizeof(Value), alignof(Value),
                                   &velocity).code == ECS_OK,
            "C API component registration failed");
    std::vector<EcsEntityId> entities(count);
    report("c-api", count, "entity-create", measure_ns(count, [&] {
        for (auto& entity : entities)
            require(ecs_entity_create(world, &entity).code == ECS_OK, "C API create failed");
    }));
    const Value initial{1, 2};
    report("c-api", count, "component-add-two", measure_ns(count * 2, [&] {
        for (const auto entity : entities) {
            require(ecs_component_add(world, entity, position, &initial,
                                      sizeof(initial)).code == ECS_OK,
                    "C API add failed");
            require(ecs_component_add(world, entity, velocity, &initial,
                                      sizeof(initial)).code == ECS_OK,
                    "C API add failed");
        }
    }));
    std::uint64_t checksum = 0;
    report("c-api", count, "component-read-write", measure_ns(count * 2, [&] {
        for (const auto entity : entities) {
            Value value{};
            require(ecs_component_read(world, entity, position, &value,
                                       sizeof(value)).code == ECS_OK,
                    "C API read failed");
            ++value.x;
            checksum += value.x;
            require(ecs_component_write(world, entity, position, &value,
                                        sizeof(value)).code == ECS_OK,
                    "C API write failed");
        }
    }));
    EcsQueryHandle one_query = 0;
    require(ecs_query_create(world, &position, 1, &one_query).code == ECS_OK,
            "C API one-component query creation failed");
    report("c-api", count, "query-one", measure_ns(count, [&] {
        bool has_value = false;
        while (ecs_query_next(one_query, &has_value).code == ECS_OK && has_value) {
            EcsEntityId entity = 0;
            require(ecs_query_entity(one_query, &entity).code == ECS_OK,
                    "C API query entity failed");
            checksum += entity != 0 ? 1U : 0U;
        }
    }));
    require(ecs_query_destroy(one_query).code == ECS_OK,
            "C API one-component query destroy failed");
    const EcsComponentTypeId required[] = {position, velocity};
    EcsQueryHandle query = 0;
    require(ecs_query_create(world, required, 2, &query).code == ECS_OK,
            "C API query creation failed");
    report("c-api", count, "query-two-read-write", measure_ns(count, [&] {
        bool has_value = false;
        while (ecs_query_next(query, &has_value).code == ECS_OK && has_value) {
            Value value{};
            require(ecs_query_component_read(query, position, &value,
                                             sizeof(value)).code == ECS_OK,
                    "C API query read failed");
            ++value.x;
            checksum += value.x;
            require(ecs_query_component_write(query, position, &value,
                                              sizeof(value)).code == ECS_OK,
                    "C API query write failed");
        }
    }));
    require(ecs_query_destroy(query).code == ECS_OK, "C API query destroy failed");
    report("c-api", count, "component-remove", measure_ns(count, [&] {
        for (const auto entity : entities)
            require(ecs_component_remove(world, entity, velocity).code == ECS_OK,
                    "C API remove failed");
    }));
    report("c-api", count, "entity-destroy", measure_ns(count, [&] {
        for (const auto entity : entities)
            require(ecs_entity_destroy(world, entity).code == ECS_OK,
                    "C API destroy failed");
    }));
    require(ecs_world_destroy(world).code == ECS_OK, "C API world destroy failed");
    if (checksum == 0) std::abort();
}

}  // namespace

int main() {
    try {
        for (const std::size_t count : {std::size_t{1000}, std::size_t{10000},
                                        std::size_t{100000}}) {
            benchmark_cpp(count);
            benchmark_c_api(count);
        }
    } catch (const std::exception& error) {
        std::cerr << "ECS benchmark failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
