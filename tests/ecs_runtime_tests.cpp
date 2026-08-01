#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "ecs/ecs_entity.hpp"
#include "ecs/ecs_entity_manager.hpp"
#include "ecs/ecs_component_pool.hpp"
#include "ecs/ecs_c_api.h"
#include "ecs/ecs_query.hpp"
#include "ecs/ecs_world.hpp"

namespace {

using mog::ecs::EcsErrorCode;
using mog::ecs::EcsWorld;
using mog::ecs::EntityManager;
using mog::ecs::ComponentPool;
using mog::ecs::EcsQuery;
using mog::ecs::entity_generation;
using mog::ecs::entity_index;
using mog::ecs::pack_entity;

class TestContext {
public:
    void expect(bool condition, const char* expression, int line) {
        if (condition) {
            return;
        }
        ++failures_;
        std::cerr << "line " << line << ": expectation failed: "
                  << expression << '\n';
    }

    int failures() const noexcept { return failures_; }

private:
    int failures_ = 0;
};

#define EXPECT(context, expression) \
    (context).expect(static_cast<bool>(expression), #expression, __LINE__)

void test_entity_packing(TestContext& test) {
    constexpr std::uint32_t index = 0x89abcdefU;
    constexpr std::uint32_t generation = 0x12345678U;
    constexpr auto entity = pack_entity(index, generation);

    EXPECT(test, entity_index(entity) == index);
    EXPECT(test, entity_generation(entity) == generation);
}

void test_entity_lifecycle(TestContext& test) {
    EntityManager entities;
    const auto first = entities.create();
    const auto second = entities.create();

    EXPECT(test, first != 0);
    EXPECT(test, first != second);
    EXPECT(test, entities.is_alive(first));
    EXPECT(test, entities.is_alive(second));
    EXPECT(test, entities.alive_count() == 2);

    EXPECT(test, entities.destroy(first));
    EXPECT(test, !entities.is_alive(first));
    EXPECT(test, !entities.destroy(first));
    EXPECT(test, entities.alive_count() == 1);

    const auto reused = entities.create();
    EXPECT(test, entity_index(reused) == entity_index(first));
    EXPECT(test, entity_generation(reused) != entity_generation(first));
    EXPECT(test, entities.is_alive(reused));
    EXPECT(test, !entities.is_alive(first));
    EXPECT(test, !entities.destroy(first));
    EXPECT(test, entities.alive_count() == 2);
}

void test_many_entities(TestContext& test) {
    constexpr std::size_t kEntityCount = 100000;
    EntityManager entities;
    std::vector<mog::ecs::EcsEntityId> original;
    original.reserve(kEntityCount);

    for (std::size_t index = 0; index < kEntityCount; ++index) {
        original.push_back(entities.create());
    }
    EXPECT(test, entities.alive_count() == kEntityCount);

    for (std::size_t index = 0; index < kEntityCount; index += 2) {
        EXPECT(test, entities.destroy(original[index]));
    }
    EXPECT(test, entities.alive_count() == kEntityCount / 2);

    for (std::size_t index = 0; index < kEntityCount / 2; ++index) {
        const auto replacement = entities.create();
        EXPECT(test, entities.is_alive(replacement));
    }
    EXPECT(test, entities.alive_count() == kEntityCount);

    for (std::size_t index = 0; index < kEntityCount; index += 2) {
        EXPECT(test, !entities.is_alive(original[index]));
    }
}

void test_world_entity_facade(TestContext& test) {
    EcsWorld world;
    const auto entity = world.create_entity();
    EXPECT(test, world.is_alive(entity));
    EXPECT(test, world.alive_count() == 1);
    EXPECT(test, world.destroy_entity(entity));
    EXPECT(test, !world.is_alive(entity));
}

void test_component_registration(TestContext& test) {
    EcsWorld world;
    const auto position = world.register_component("Position", 8, 4);
    const auto velocity = world.register_component("Velocity", 8, 4);

    EXPECT(test, position.ok());
    EXPECT(test, velocity.ok());
    EXPECT(test, position.component_type != 0);
    EXPECT(test, velocity.component_type != position.component_type);
    EXPECT(test, world.component_type_count() == 2);

    const auto* by_id = world.find_component_type(position.component_type);
    EXPECT(test, by_id != nullptr);
    if (by_id != nullptr) {
        EXPECT(test, by_id->name == "Position");
        EXPECT(test, by_id->size == 8);
        EXPECT(test, by_id->alignment == 4);
    }

    const auto* by_name = world.find_component_type("Velocity");
    EXPECT(test, by_name != nullptr);
    if (by_name != nullptr) {
        EXPECT(test, by_name->id == velocity.component_type);
    }
    EXPECT(test, world.find_component_type(0) == nullptr);
    EXPECT(test, world.find_component_type("Missing") == nullptr);
}

void test_component_registration_validation(TestContext& test) {
    EcsWorld world;

    EXPECT(test, world.register_component("", 4, 4).error ==
                     EcsErrorCode::InvalidArgument);
    EXPECT(test, world.register_component("Zero", 0, 4).error ==
                     EcsErrorCode::InvalidComponentSize);
    EXPECT(test, world.register_component("AlignZero", 4, 0).error ==
                     EcsErrorCode::InvalidAlignment);
    EXPECT(test, world.register_component("AlignThree", 4, 3).error ==
                     EcsErrorCode::InvalidAlignment);
    EXPECT(test, world.register_component("AlignSix", 8, 6).error ==
                     EcsErrorCode::InvalidAlignment);
    EXPECT(test, world.component_type_count() == 0);

    const auto byte = world.register_component("Byte", 1, 1);
    const auto wide = world.register_component("Wide", 32, 32);
    EXPECT(test, byte.ok());
    EXPECT(test, wide.ok());
    EXPECT(test, byte.component_type == 1);
    EXPECT(test, wide.component_type == 2);
}

void test_duplicate_component_names(TestContext& test) {
    EcsWorld world;
    const auto first = world.register_component("Position", 8, 4);
    const auto duplicate = world.register_component("Position", 8, 4);
    const auto size_conflict = world.register_component("Position", 16, 4);
    const auto alignment_conflict =
        world.register_component("Position", 8, 8);

    EXPECT(test, first.ok());
    EXPECT(test, duplicate.ok());
    EXPECT(test, duplicate.component_type == first.component_type);
    EXPECT(test, world.component_type_count() == 1);
    EXPECT(test, size_conflict.error == EcsErrorCode::ComponentTypeConflict);
    EXPECT(test,
           alignment_conflict.error == EcsErrorCode::ComponentTypeConflict);

    EcsWorld another_world;
    const auto independent =
        another_world.register_component("Position", 16, 8);
    EXPECT(test, independent.ok());
    EXPECT(test, independent.component_type == 1);
}

struct Position {
    std::int32_t x;
    std::int32_t y;
};

void test_component_pool(TestContext& test) {
    ComponentPool pool({1, "Position", sizeof(Position), alignof(Position)});
    const auto first = pack_entity(2, 1);
    const auto middle = pack_entity(8, 1);
    const auto last = pack_entity(20, 4);
    const Position first_value{10, 20};
    const Position middle_value{30, 40};
    const Position last_value{50, 60};

    EXPECT(test, pool.add(first, &first_value, sizeof(first_value)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, pool.add(middle, &middle_value, sizeof(middle_value)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, pool.add(last, &last_value, sizeof(last_value)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, pool.size() == 3);
    EXPECT(test, pool.contains(first));
    EXPECT(test, pool.contains(middle));
    EXPECT(test, pool.contains(last));
    EXPECT(test, pool.add(first, &first_value, sizeof(first_value)) ==
                     EcsErrorCode::ComponentAlreadyExists);

    Position read_value{};
    EXPECT(test, pool.read(middle, &read_value, sizeof(read_value)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, read_value.x == 30 && read_value.y == 40);

    const Position updated{31, 41};
    EXPECT(test, pool.write(middle, &updated, sizeof(updated)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, pool.read(middle, &read_value, sizeof(read_value)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, read_value.x == 31 && read_value.y == 41);

    EXPECT(test, pool.remove(middle));
    EXPECT(test, !pool.contains(middle));
    EXPECT(test, pool.contains(last));
    EXPECT(test, pool.size() == 2);
    EXPECT(test, pool.read(last, &read_value, sizeof(read_value)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, read_value.x == 50 && read_value.y == 60);
    EXPECT(test, !pool.remove(middle));

    const auto stale_last = pack_entity(entity_index(last), 3);
    EXPECT(test, !pool.contains(stale_last));
    EXPECT(test, pool.read(stale_last, &read_value, sizeof(read_value)) ==
                     EcsErrorCode::ComponentNotFound);
    EXPECT(test, pool.read(last, &read_value, sizeof(read_value) - 1) ==
                     EcsErrorCode::InvalidComponentSize);
    EXPECT(test, pool.write(last, nullptr, sizeof(read_value)) ==
                     EcsErrorCode::InvalidArgument);
}

void test_component_pool_sparse_churn(TestContext& test) {
    ComponentPool pool({1, "Value", sizeof(std::uint32_t),
                        alignof(std::uint32_t)});
    constexpr std::uint32_t kCount = 2000;
    for (std::uint32_t index = 0; index < kCount; ++index) {
        const auto entity = pack_entity(index * 3U, 1);
        EXPECT(test, pool.add(entity, &index, sizeof(index)) ==
                         EcsErrorCode::Ok);
    }
    for (std::uint32_t index = 0; index < kCount; index += 3) {
        EXPECT(test, pool.remove(pack_entity(index * 3U, 1)));
    }
    for (std::uint32_t index = 0; index < kCount; ++index) {
        const auto entity = pack_entity(index * 3U, 1);
        if (index % 3U == 0) {
            EXPECT(test, !pool.contains(entity));
            continue;
        }
        std::uint32_t value = 0;
        EXPECT(test, pool.read(entity, &value, sizeof(value)) ==
                         EcsErrorCode::Ok);
        EXPECT(test, value == index);
    }
}

void test_world_components_and_versions(TestContext& test) {
    EcsWorld world;
    EXPECT(test, world.structural_version() == 0);
    const auto position_type =
        world.register_component("Position", sizeof(Position),
                                 alignof(Position));
    EXPECT(test, position_type.ok());
    EXPECT(test, world.structural_version() == 1);
    const auto duplicate =
        world.register_component("Position", sizeof(Position),
                                 alignof(Position));
    EXPECT(test, duplicate.ok());
    EXPECT(test, world.structural_version() == 1);

    const auto entity = world.create_entity();
    EXPECT(test, world.structural_version() == 2);
    const Position initial{4, 5};
    EXPECT(test, world.add_component(entity, position_type.component_type,
                                     &initial, sizeof(initial)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, world.structural_version() == 3);

    bool has = false;
    EXPECT(test, world.has_component(entity, position_type.component_type,
                                     has) == EcsErrorCode::Ok);
    EXPECT(test, has);
    Position value{};
    EXPECT(test, world.read_component(entity, position_type.component_type,
                                      &value, sizeof(value)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, value.x == 4 && value.y == 5);

    const Position updated{9, 12};
    EXPECT(test, world.write_component(entity, position_type.component_type,
                                       &updated, sizeof(updated)) ==
                     EcsErrorCode::Ok);
    EXPECT(test, world.structural_version() == 3);
    EXPECT(test, world.remove_component(entity, position_type.component_type) ==
                     EcsErrorCode::Ok);
    EXPECT(test, world.structural_version() == 4);
    EXPECT(test, world.remove_component(entity, position_type.component_type) ==
                     EcsErrorCode::ComponentNotFound);
}

void test_world_destroy_cleanup_and_stale_entities(TestContext& test) {
    EcsWorld world;
    const auto type = world.register_component("Position", sizeof(Position),
                                               alignof(Position));
    const auto old_entity = world.create_entity();
    const Position value{1, 2};
    EXPECT(test, world.add_component(old_entity, type.component_type, &value,
                                     sizeof(value)) == EcsErrorCode::Ok);
    EXPECT(test, world.destroy_entity(old_entity));
    EXPECT(test, world.find_pool(type.component_type)->size() == 0);

    const auto replacement = world.create_entity();
    EXPECT(test, entity_index(replacement) == entity_index(old_entity));
    EXPECT(test, world.add_component(old_entity, type.component_type, &value,
                                     sizeof(value)) ==
                     EcsErrorCode::StaleEntity);
    bool has = true;
    EXPECT(test, world.has_component(replacement, type.component_type, has) ==
                     EcsErrorCode::Ok);
    EXPECT(test, !has);
}

void test_queries(TestContext& test) {
    EcsWorld world;
    const auto position = world.register_component(
        "Position", sizeof(Position), alignof(Position));
    const auto velocity = world.register_component(
        "Velocity", sizeof(Position), alignof(Position));
    const auto health = world.register_component(
        "Health", sizeof(std::int32_t), alignof(std::int32_t));

    std::vector<mog::ecs::EcsEntityId> entities;
    for (std::int32_t index = 0; index < 4; ++index) {
        const auto entity = world.create_entity();
        entities.push_back(entity);
        const Position position_value{index, index + 10};
        EXPECT(test, world.add_component(entity, position.component_type,
                                         &position_value,
                                         sizeof(position_value)) ==
                         EcsErrorCode::Ok);
        if (index < 2) {
            const Position velocity_value{1, 2};
            EXPECT(test, world.add_component(entity, velocity.component_type,
                                             &velocity_value,
                                             sizeof(velocity_value)) ==
                             EcsErrorCode::Ok);
        }
        if (index == 0 || index == 2) {
            const std::int32_t health_value = 100 - index;
            EXPECT(test, world.add_component(entity, health.component_type,
                                             &health_value,
                                             sizeof(health_value)) ==
                             EcsErrorCode::Ok);
        }
    }

    auto count_matches = [&](const std::vector<mog::ecs::EcsComponentTypeId>&
                                 required) {
        std::unique_ptr<EcsQuery> query;
        EXPECT(test, EcsQuery::create(world, required.data(),
                                      static_cast<std::uint32_t>(required.size()),
                                      query) == EcsErrorCode::Ok);
        std::size_t count = 0;
        bool has_value = false;
        while (query->next(has_value) == EcsErrorCode::Ok && has_value) {
            mog::ecs::EcsEntityId current = 0;
            EXPECT(test, query->entity(current) == EcsErrorCode::Ok);
            EXPECT(test, world.is_alive(current));
            ++count;
        }
        return count;
    };

    EXPECT(test, count_matches({position.component_type}) == 4);
    EXPECT(test, count_matches({position.component_type,
                                velocity.component_type}) == 2);
    EXPECT(test, count_matches({position.component_type,
                                velocity.component_type,
                                health.component_type}) == 1);

    std::unique_ptr<EcsQuery> query;
    const mog::ecs::EcsComponentTypeId movement_types[] = {
        position.component_type, velocity.component_type};
    EXPECT(test, EcsQuery::create(world, movement_types, 2, query) ==
                     EcsErrorCode::Ok);
    bool has_value = false;
    EXPECT(test, query->next(has_value) == EcsErrorCode::Ok && has_value);
    Position current{};
    EXPECT(test, query->read_component(position.component_type, &current,
                                       sizeof(current)) == EcsErrorCode::Ok);
    current.x += 100;
    EXPECT(test, query->write_component(position.component_type, &current,
                                        sizeof(current)) == EcsErrorCode::Ok);
    EXPECT(test, query->next(has_value) == EcsErrorCode::Ok && has_value);

    const Position extra{7, 8};
    EXPECT(test, world.add_component(entities[3], velocity.component_type,
                                     &extra, sizeof(extra)) == EcsErrorCode::Ok);
    EXPECT(test, query->next(has_value) == EcsErrorCode::QueryInvalidated);
    mog::ecs::EcsEntityId current_entity = 0;
    EXPECT(test, query->entity(current_entity) ==
                     EcsErrorCode::QueryInvalidated);
}

void test_query_validation_and_empty_results(TestContext& test) {
    EcsWorld world;
    const auto first =
        world.register_component("First", sizeof(std::uint32_t),
                                 alignof(std::uint32_t));
    const auto second =
        world.register_component("Second", sizeof(std::uint32_t),
                                 alignof(std::uint32_t));
    std::unique_ptr<EcsQuery> query;

    EXPECT(test, EcsQuery::create(world, nullptr, 0, query) ==
                     EcsErrorCode::InvalidArgument);
    const mog::ecs::EcsComponentTypeId invalid[] = {999};
    EXPECT(test, EcsQuery::create(world, invalid, 1, query) ==
                     EcsErrorCode::InvalidComponentType);
    const mog::ecs::EcsComponentTypeId duplicate[] = {
        first.component_type, first.component_type};
    EXPECT(test, EcsQuery::create(world, duplicate, 2, query) ==
                     EcsErrorCode::InvalidArgument);

    const mog::ecs::EcsComponentTypeId intersection[] = {
        first.component_type, second.component_type};
    EXPECT(test, EcsQuery::create(world, intersection, 2, query) ==
                     EcsErrorCode::Ok);
    bool has_value = true;
    EXPECT(test, query->next(has_value) == EcsErrorCode::Ok);
    EXPECT(test, !has_value);
    mog::ecs::EcsEntityId entity = 0;
    EXPECT(test, query->entity(entity) == EcsErrorCode::InvalidArgument);
}

void test_c_api(TestContext& test) {
    EcsWorldHandle world = 0;
    EXPECT(test, ecs_world_create(&world).code == ECS_OK);
    EXPECT(test, world != 0);

    EcsComponentTypeId position = 0;
    EXPECT(test, ecs_component_register(world, "Position", sizeof(Position),
                                        alignof(Position), &position)
                     .code == ECS_OK);
    EcsEntityId first = 0;
    EcsEntityId second = 0;
    EXPECT(test, ecs_entity_create(world, &first).code == ECS_OK);
    EXPECT(test, ecs_entity_create(world, &second).code == ECS_OK);
    const Position first_position{3, 4};
    const Position second_position{8, 9};
    EXPECT(test, ecs_component_add(world, first, position, &first_position,
                                   sizeof(first_position))
                     .code == ECS_OK);
    EXPECT(test, ecs_component_add(world, second, position, &second_position,
                                   sizeof(second_position))
                     .code == ECS_OK);

    EcsQueryHandle query = 0;
    EXPECT(test, ecs_query_create(world, &position, 1, &query).code == ECS_OK);
    bool has_value = false;
    EXPECT(test, ecs_query_next(query, &has_value).code == ECS_OK);
    EXPECT(test, has_value);
    EcsEntityId current = 0;
    EXPECT(test, ecs_query_entity(query, &current).code == ECS_OK);
    Position value{};
    EXPECT(test, ecs_query_component_read(query, position, &value,
                                          sizeof(value))
                     .code == ECS_OK);
    value.x += 10;
    EXPECT(test, ecs_query_component_write(query, position, &value,
                                           sizeof(value))
                     .code == ECS_OK);
    EXPECT(test, ecs_query_next(query, &has_value).code == ECS_OK);
    EXPECT(test, has_value);

    EXPECT(test, ecs_component_remove(world, second, position).code == ECS_OK);
    EXPECT(test, ecs_query_next(query, &has_value).code ==
                     ECS_QUERY_INVALIDATED);
    EXPECT(test, std::string(ecs_last_error_message()).find("invalidated") !=
                     std::string::npos);
    EXPECT(test, ecs_query_destroy(query).code == ECS_OK);
    EXPECT(test, ecs_query_destroy(query).code == ECS_INVALID_QUERY);

    EcsQueryHandle owned_query = 0;
    EXPECT(test, ecs_query_create(world, &position, 1, &owned_query).code ==
                     ECS_OK);
    EXPECT(test, ecs_world_destroy(world).code == ECS_OK);
    EXPECT(test, ecs_query_next(owned_query, &has_value).code ==
                     ECS_INVALID_QUERY);
    EXPECT(test, ecs_entity_create(world, &current).code == ECS_INVALID_WORLD);

    EcsWorldHandle replacement_world = 0;
    EXPECT(test, ecs_world_create(&replacement_world).code == ECS_OK);
    EXPECT(test, replacement_world != world);
    EXPECT(test, ecs_world_destroy(replacement_world).code == ECS_OK);
    EXPECT(test, ecs_world_create(nullptr).code == ECS_INVALID_ARGUMENT);
}

}  // namespace

int main() {
    TestContext test;
    test_entity_packing(test);
    test_entity_lifecycle(test);
    test_many_entities(test);
    test_world_entity_facade(test);
    test_component_registration(test);
    test_component_registration_validation(test);
    test_duplicate_component_names(test);
    test_component_pool(test);
    test_component_pool_sparse_churn(test);
    test_world_components_and_versions(test);
    test_world_destroy_cleanup_and_stale_entities(test);
    test_queries(test);
    test_query_validation_and_empty_results(test);
    test_c_api(test);

    if (test.failures() != 0) {
        std::cerr << test.failures() << " ECS runtime test(s) failed\n";
        return 1;
    }

    std::cout << "ECS native runtime tests passed\n";
    return 0;
}
