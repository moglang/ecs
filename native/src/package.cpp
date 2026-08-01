#include "NativePackageAPI.hpp"
#include "ecs/ecs_c_api.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kNamespace = "github";
constexpr std::string_view kPackage = "ecs-native";

struct WorldPayload {
    EcsWorldHandle handle = 0;
};

struct QueryPayload {
    EcsQueryHandle handle = 0;
};

thread_local std::string error_storage;
thread_local std::vector<std::uint8_t> byte_result_storage;

bool fail_literal(ExprPackageStringView* out_error,
                  const char* message) noexcept {
    if (out_error != nullptr) {
        *out_error = {message, std::char_traits<char>::length(message)};
    }
    return false;
}

bool fail(ExprPackageStringView* out_error, std::string message) {
    error_storage = std::move(message);
    if (out_error != nullptr) {
        *out_error = {error_storage.data(), error_storage.size()};
    }
    return false;
}

bool fail_ecs(ExprPackageStringView* out_error, std::string_view operation,
              EcsResult result) {
    std::string message(operation);
    message.append(": ");
    const char* detail = ecs_last_error_message();
    message.append(detail == nullptr || detail[0] == '\0' ? "ECS operation failed"
                                                          : detail);
    message.append(" (error ");
    message.append(std::to_string(static_cast<unsigned int>(result.code)));
    message.push_back(')');
    return fail(out_error, std::move(message));
}

template <typename Function>
bool protect(ExprPackageStringView* out_error, Function&& function) noexcept {
    try {
        return function();
    } catch (const std::bad_alloc&) {
        return fail_literal(out_error, "native ECS package allocation failed");
    } catch (const std::exception&) {
        return fail_literal(out_error, "native ECS package operation failed");
    } catch (...) {
        return fail_literal(out_error, "unknown native ECS package error");
    }
}

bool check_call(const char* operation, EcsResult result,
                ExprPackageStringView* out_error) {
    return result.code == ECS_OK || fail_ecs(out_error, operation, result);
}

bool check_arguments(const char* operation, const ExprPackageValue* args,
                     std::size_t argc, std::size_t expected,
                     ExprPackageValue* out_result,
                     ExprPackageStringView* out_error) {
    if (argc == expected && (expected == 0 || args != nullptr) &&
        out_result != nullptr) {
        return true;
    }
    return fail(out_error, std::string(operation) + ": expected exactly " +
                               std::to_string(expected) + " arguments");
}

template <typename Payload>
bool get_handle(const ExprPackageValue& value, const char* type_name,
                Payload*& out_payload, ExprPackageStringView* out_error) {
    out_payload = nullptr;
    if (value.kind != EXPR_PACKAGE_VALUE_HANDLE) {
        return fail(out_error, std::string("expected ") + type_name);
    }
    const ExprPackageHandleValue& handle = value.as.handle_value;
    if (handle.package_namespace == nullptr || handle.package_name == nullptr ||
        handle.type_name == nullptr || handle.handle_data == nullptr ||
        std::string_view(handle.package_namespace) != kNamespace ||
        std::string_view(handle.package_name) != kPackage ||
        std::string_view(handle.type_name) != type_name) {
        return fail(out_error,
                    std::string("expected github:ecs-native ") + type_name);
    }
    out_payload = static_cast<Payload*>(handle.handle_data);
    return true;
}

bool get_world(const ExprPackageValue& value, WorldPayload*& out_world,
               ExprPackageStringView* out_error) {
    if (!get_handle(value, "WorldHandle", out_world, out_error)) {
        return false;
    }
    return out_world->handle != 0 || fail(out_error, "world handle is closed");
}

bool get_query(const ExprPackageValue& value, QueryPayload*& out_query,
               ExprPackageStringView* out_error) {
    if (!get_handle(value, "QueryHandle", out_query, out_error)) {
        return false;
    }
    return out_query->handle != 0 || fail(out_error, "query handle is closed");
}

bool get_u64(const ExprPackageValue& value, const char* description,
             std::uint64_t& out_value, ExprPackageStringView* out_error) {
    if (value.kind != EXPR_PACKAGE_VALUE_U64) {
        return fail(out_error, std::string("expected u64 ") + description);
    }
    out_value = value.as.u64_value;
    return true;
}

bool get_i64(const ExprPackageValue& value, const char* description,
             std::int64_t& out_value, ExprPackageStringView* out_error) {
    if (value.kind != EXPR_PACKAGE_VALUE_I64) {
        return fail(out_error, std::string("expected i64 ") + description);
    }
    out_value = value.as.i64_value;
    return true;
}

bool get_f64(const ExprPackageValue& value, const char* description,
             double& out_value, ExprPackageStringView* out_error) {
    if (value.kind != EXPR_PACKAGE_VALUE_F64) {
        return fail(out_error, std::string("expected f64 ") + description);
    }
    out_value = value.as.f64_value;
    return true;
}

bool get_codec_width(const ExprPackageValue& value, std::size_t& out_width,
                     ExprPackageStringView* out_error) {
    std::uint64_t width = 0;
    if (!get_u64(value, "codec width", width, out_error)) {
        return false;
    }
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        return fail(out_error, "codec width must be 1, 2, 4, or 8 bytes");
    }
    out_width = static_cast<std::size_t>(width);
    return true;
}

bool narrow_u32(std::uint64_t value, const char* description,
                std::uint32_t& out_value, ExprPackageStringView* out_error) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return fail(out_error, std::string(description) + " exceeds u32 range");
    }
    out_value = static_cast<std::uint32_t>(value);
    return true;
}

bool get_component_type(const ExprPackageValue& value,
                        EcsComponentTypeId& out_type,
                        ExprPackageStringView* out_error) {
    std::uint64_t raw = 0;
    return get_u64(value, "component type", raw, out_error) &&
           narrow_u32(raw, "component type", out_type, out_error);
}

bool get_bytes(const ExprPackageValue& value, ExprPackageByteView& out_bytes,
               ExprPackageStringView* out_error) {
    if (value.kind != EXPR_PACKAGE_VALUE_BYTES ||
        (value.as.bytes_value.data == nullptr &&
         value.as.bytes_value.length != 0)) {
        return fail(out_error, "expected Array<u8> byte buffer");
    }
    out_bytes = value.as.bytes_value;
    return true;
}

bool byte_count_u32(std::size_t size, std::uint32_t& out_size,
                    ExprPackageStringView* out_error) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        return fail(out_error, "byte buffer length exceeds u32 range");
    }
    out_size = static_cast<std::uint32_t>(size);
    return true;
}

void set_null(ExprPackageValue* out_result) {
    out_result->kind = EXPR_PACKAGE_VALUE_NULL;
}

void set_byte_result(std::uint64_t bits, std::size_t width,
                     ExprPackageValue* out_result) {
    byte_result_storage.resize(width);
    for (std::size_t index = 0; index < width; ++index) {
        byte_result_storage[index] =
            static_cast<std::uint8_t>((bits >> (index * 8U)) & 0xffU);
    }
    out_result->kind = EXPR_PACKAGE_VALUE_BYTES;
    out_result->as.bytes_value = {byte_result_storage.data(), width};
}

bool read_codec_bits(const ExprPackageValue& bytes_value,
                     const ExprPackageValue& offset_value,
                     const ExprPackageValue& width_value,
                     std::uint64_t& out_bits, std::size_t& out_width,
                     ExprPackageStringView* out_error) {
    ExprPackageByteView bytes{};
    std::uint64_t raw_offset = 0;
    if (!get_bytes(bytes_value, bytes, out_error) ||
        !get_u64(offset_value, "codec offset", raw_offset, out_error) ||
        !get_codec_width(width_value, out_width, out_error)) {
        return false;
    }
    if (raw_offset > bytes.length || out_width > bytes.length - raw_offset) {
        return fail(out_error, "codec read exceeds byte buffer bounds");
    }
    const std::size_t offset = static_cast<std::size_t>(raw_offset);
    out_bits = 0;
    for (std::size_t index = 0; index < out_width; ++index) {
        out_bits |= static_cast<std::uint64_t>(bytes.data[offset + index])
                    << (index * 8U);
    }
    return true;
}

void release_world(void* data) noexcept {
    auto* payload = static_cast<WorldPayload*>(data);
    if (payload != nullptr && payload->handle != 0) {
        (void)ecs_world_destroy(payload->handle);
        payload->handle = 0;
    }
    delete payload;
}

void release_query(void* data) noexcept {
    auto* payload = static_cast<QueryPayload*>(data);
    if (payload != nullptr && payload->handle != 0) {
        (void)ecs_query_destroy(payload->handle);
        payload->handle = 0;
    }
    delete payload;
}

bool world_create(const ExprHostApi*, const ExprPackageValue* args,
                  std::size_t argc, ExprPackageValue* out_result,
                  ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("worldCreate", args, argc, 0, out_result,
                             out_error)) {
            return false;
        }
        auto* payload = new (std::nothrow) WorldPayload();
        if (payload == nullptr) {
            return fail(out_error, "worldCreate: allocation failed");
        }
        const EcsResult result = ecs_world_create(&payload->handle);
        if (result.code != ECS_OK) {
            delete payload;
            return fail_ecs(out_error, "worldCreate", result);
        }
        out_result->kind = EXPR_PACKAGE_VALUE_HANDLE;
        out_result->as.handle_value = {"github", "ecs-native", "WorldHandle", payload,
                                       release_world};
        return true;
    });
}

bool world_destroy(const ExprHostApi*, const ExprPackageValue* args,
                   std::size_t argc, ExprPackageValue* out_result,
                   ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("worldDestroy", args, argc, 1, out_result,
                             out_error)) {
            return false;
        }
        WorldPayload* world = nullptr;
        if (!get_world(args[0], world, out_error)) {
            return false;
        }
        const EcsResult result = ecs_world_destroy(world->handle);
        if (!check_call("worldDestroy", result, out_error)) {
            return false;
        }
        world->handle = 0;
        set_null(out_result);
        return true;
    });
}

bool entity_create(const ExprHostApi*, const ExprPackageValue* args,
                   std::size_t argc, ExprPackageValue* out_result,
                   ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("entityCreate", args, argc, 1, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        EcsEntityId entity = 0;
        if (!get_world(args[0], world, out_error) ||
            !check_call("entityCreate", ecs_entity_create(world->handle, &entity),
                        out_error)) return false;
        out_result->kind = EXPR_PACKAGE_VALUE_U64;
        out_result->as.u64_value = entity;
        return true;
    });
}

bool entity_destroy(const ExprHostApi*, const ExprPackageValue* args,
                    std::size_t argc, ExprPackageValue* out_result,
                    ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("entityDestroy", args, argc, 2, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        std::uint64_t entity = 0;
        if (!get_world(args[0], world, out_error) ||
            !get_u64(args[1], "entity", entity, out_error) ||
            !check_call("entityDestroy",
                        ecs_entity_destroy(world->handle, entity), out_error))
            return false;
        set_null(out_result);
        return true;
    });
}

bool entity_is_alive(const ExprHostApi*, const ExprPackageValue* args,
                     std::size_t argc, ExprPackageValue* out_result,
                     ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("entityIsAlive", args, argc, 2, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        std::uint64_t entity = 0;
        bool alive = false;
        if (!get_world(args[0], world, out_error) ||
            !get_u64(args[1], "entity", entity, out_error) ||
            !check_call("entityIsAlive",
                        ecs_entity_is_alive(world->handle, entity, &alive),
                        out_error)) return false;
        out_result->kind = EXPR_PACKAGE_VALUE_BOOL;
        out_result->as.boolean_value = alive;
        return true;
    });
}

bool component_register(const ExprHostApi*, const ExprPackageValue* args,
                        std::size_t argc, ExprPackageValue* out_result,
                        ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("componentRegister", args, argc, 4, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        if (!get_world(args[0], world, out_error)) return false;
        if (args[1].kind != EXPR_PACKAGE_VALUE_STR ||
            (args[1].as.string_value.data == nullptr &&
             args[1].as.string_value.length != 0))
            return fail(out_error, "componentRegister: expected component name str");
        std::uint64_t raw_size = 0;
        std::uint64_t raw_alignment = 0;
        std::uint32_t size = 0;
        std::uint32_t alignment = 0;
        if (!get_u64(args[2], "component size", raw_size, out_error) ||
            !get_u64(args[3], "component alignment", raw_alignment, out_error) ||
            !narrow_u32(raw_size, "component size", size, out_error) ||
            !narrow_u32(raw_alignment, "component alignment", alignment,
                        out_error)) return false;
        const ExprPackageStringView view = args[1].as.string_value;
        const std::string name(view.data == nullptr ? "" : view.data, view.length);
        EcsComponentTypeId component_type = 0;
        if (!check_call("componentRegister",
                        ecs_component_register(world->handle, name.c_str(), size,
                                               alignment, &component_type),
                        out_error)) return false;
        out_result->kind = EXPR_PACKAGE_VALUE_U64;
        out_result->as.u64_value = component_type;
        return true;
    });
}

bool component_add(const ExprHostApi*, const ExprPackageValue* args,
                   std::size_t argc, ExprPackageValue* out_result,
                   ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("componentAdd", args, argc, 4, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        std::uint64_t entity = 0;
        EcsComponentTypeId type = 0;
        ExprPackageByteView bytes{};
        std::uint32_t size = 0;
        if (!get_world(args[0], world, out_error) ||
            !get_u64(args[1], "entity", entity, out_error) ||
            !get_component_type(args[2], type, out_error) ||
            !get_bytes(args[3], bytes, out_error) ||
            !byte_count_u32(bytes.length, size, out_error) ||
            !check_call("componentAdd",
                        ecs_component_add(world->handle, entity, type, bytes.data,
                                          size), out_error)) return false;
        set_null(out_result);
        return true;
    });
}

bool component_remove(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("componentRemove", args, argc, 3, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        std::uint64_t entity = 0;
        EcsComponentTypeId type = 0;
        if (!get_world(args[0], world, out_error) ||
            !get_u64(args[1], "entity", entity, out_error) ||
            !get_component_type(args[2], type, out_error) ||
            !check_call("componentRemove",
                        ecs_component_remove(world->handle, entity, type),
                        out_error)) return false;
        set_null(out_result);
        return true;
    });
}

bool component_has(const ExprHostApi*, const ExprPackageValue* args,
                   std::size_t argc, ExprPackageValue* out_result,
                   ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("componentHas", args, argc, 3, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        std::uint64_t entity = 0;
        EcsComponentTypeId type = 0;
        bool has = false;
        if (!get_world(args[0], world, out_error) ||
            !get_u64(args[1], "entity", entity, out_error) ||
            !get_component_type(args[2], type, out_error) ||
            !check_call("componentHas",
                        ecs_component_has(world->handle, entity, type, &has),
                        out_error)) return false;
        out_result->kind = EXPR_PACKAGE_VALUE_BOOL;
        out_result->as.boolean_value = has;
        return true;
    });
}

bool component_read(const ExprHostApi*, const ExprPackageValue* args,
                    std::size_t argc, ExprPackageValue* out_result,
                    ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("componentRead", args, argc, 4, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        std::uint64_t entity = 0;
        EcsComponentTypeId type = 0;
        std::uint64_t raw_size = 0;
        std::uint32_t size = 0;
        if (!get_world(args[0], world, out_error) ||
            !get_u64(args[1], "entity", entity, out_error) ||
            !get_component_type(args[2], type, out_error) ||
            !get_u64(args[3], "read size", raw_size, out_error) ||
            !narrow_u32(raw_size, "read size", size, out_error)) return false;
        byte_result_storage.resize(size);
        if (!check_call("componentRead",
                        ecs_component_read(world->handle, entity, type,
                                           byte_result_storage.data(), size),
                        out_error)) return false;
        out_result->kind = EXPR_PACKAGE_VALUE_BYTES;
        out_result->as.bytes_value = {
            byte_result_storage.empty() ? nullptr : byte_result_storage.data(),
            byte_result_storage.size()};
        return true;
    });
}

bool component_write(const ExprHostApi*, const ExprPackageValue* args,
                     std::size_t argc, ExprPackageValue* out_result,
                     ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("componentWrite", args, argc, 4, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        std::uint64_t entity = 0;
        EcsComponentTypeId type = 0;
        ExprPackageByteView bytes{};
        std::uint32_t size = 0;
        if (!get_world(args[0], world, out_error) ||
            !get_u64(args[1], "entity", entity, out_error) ||
            !get_component_type(args[2], type, out_error) ||
            !get_bytes(args[3], bytes, out_error) ||
            !byte_count_u32(bytes.length, size, out_error) ||
            !check_call("componentWrite",
                        ecs_component_write(world->handle, entity, type,
                                            bytes.data, size), out_error))
            return false;
        set_null(out_result);
        return true;
    });
}

bool query_create(const ExprHostApi*, const ExprPackageValue* args,
                  std::size_t argc, ExprPackageValue* out_result,
                  ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("queryCreate", args, argc, 2, out_result,
                             out_error)) return false;
        WorldPayload* world = nullptr;
        ExprPackageByteView bytes{};
        if (!get_world(args[0], world, out_error) ||
            !get_bytes(args[1], bytes, out_error)) return false;
        if (bytes.length % 4 != 0)
            return fail(out_error,
                        "queryCreate: component ID buffer length must be a multiple of 4");
        const std::size_t count = bytes.length / 4;
        if (count > std::numeric_limits<std::uint32_t>::max())
            return fail(out_error, "queryCreate: too many component IDs");
        std::vector<EcsComponentTypeId> types;
        types.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t offset = index * 4;
            const std::uint32_t value =
                static_cast<std::uint32_t>(bytes.data[offset]) |
                (static_cast<std::uint32_t>(bytes.data[offset + 1]) << 8U) |
                (static_cast<std::uint32_t>(bytes.data[offset + 2]) << 16U) |
                (static_cast<std::uint32_t>(bytes.data[offset + 3]) << 24U);
            types.push_back(value);
        }
        auto* payload = new (std::nothrow) QueryPayload();
        if (payload == nullptr) return fail(out_error, "queryCreate: allocation failed");
        const EcsResult result = ecs_query_create(
            world->handle, types.empty() ? nullptr : types.data(),
            static_cast<std::uint32_t>(types.size()), &payload->handle);
        if (result.code != ECS_OK) {
            delete payload;
            return fail_ecs(out_error, "queryCreate", result);
        }
        out_result->kind = EXPR_PACKAGE_VALUE_HANDLE;
        out_result->as.handle_value = {"github", "ecs-native", "QueryHandle", payload,
                                       release_query};
        return true;
    });
}

bool query_next(const ExprHostApi*, const ExprPackageValue* args,
                std::size_t argc, ExprPackageValue* out_result,
                ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("queryNext", args, argc, 1, out_result,
                             out_error)) return false;
        QueryPayload* query = nullptr;
        bool has_value = false;
        if (!get_query(args[0], query, out_error) ||
            !check_call("queryNext", ecs_query_next(query->handle, &has_value),
                        out_error)) return false;
        out_result->kind = EXPR_PACKAGE_VALUE_BOOL;
        out_result->as.boolean_value = has_value;
        return true;
    });
}

bool query_entity(const ExprHostApi*, const ExprPackageValue* args,
                  std::size_t argc, ExprPackageValue* out_result,
                  ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("queryEntity", args, argc, 1, out_result,
                             out_error)) return false;
        QueryPayload* query = nullptr;
        EcsEntityId entity = 0;
        if (!get_query(args[0], query, out_error) ||
            !check_call("queryEntity",
                        ecs_query_entity(query->handle, &entity), out_error))
            return false;
        out_result->kind = EXPR_PACKAGE_VALUE_U64;
        out_result->as.u64_value = entity;
        return true;
    });
}

bool query_read(const ExprHostApi*, const ExprPackageValue* args,
                std::size_t argc, ExprPackageValue* out_result,
                ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("queryRead", args, argc, 3, out_result,
                             out_error)) return false;
        QueryPayload* query = nullptr;
        EcsComponentTypeId type = 0;
        std::uint64_t raw_size = 0;
        std::uint32_t size = 0;
        if (!get_query(args[0], query, out_error) ||
            !get_component_type(args[1], type, out_error) ||
            !get_u64(args[2], "read size", raw_size, out_error) ||
            !narrow_u32(raw_size, "read size", size, out_error)) return false;
        byte_result_storage.resize(size);
        if (!check_call("queryRead",
                        ecs_query_component_read(query->handle, type,
                                                 byte_result_storage.data(), size),
                        out_error)) return false;
        out_result->kind = EXPR_PACKAGE_VALUE_BYTES;
        out_result->as.bytes_value = {
            byte_result_storage.empty() ? nullptr : byte_result_storage.data(),
            byte_result_storage.size()};
        return true;
    });
}

bool query_write(const ExprHostApi*, const ExprPackageValue* args,
                 std::size_t argc, ExprPackageValue* out_result,
                 ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("queryWrite", args, argc, 3, out_result,
                             out_error)) return false;
        QueryPayload* query = nullptr;
        EcsComponentTypeId type = 0;
        ExprPackageByteView bytes{};
        std::uint32_t size = 0;
        if (!get_query(args[0], query, out_error) ||
            !get_component_type(args[1], type, out_error) ||
            !get_bytes(args[2], bytes, out_error) ||
            !byte_count_u32(bytes.length, size, out_error) ||
            !check_call("queryWrite",
                        ecs_query_component_write(query->handle, type, bytes.data,
                                                  size), out_error)) return false;
        set_null(out_result);
        return true;
    });
}

bool query_destroy(const ExprHostApi*, const ExprPackageValue* args,
                   std::size_t argc, ExprPackageValue* out_result,
                   ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("queryDestroy", args, argc, 1, out_result,
                             out_error)) return false;
        QueryPayload* query = nullptr;
        if (!get_query(args[0], query, out_error)) return false;
        const EcsResult result = ecs_query_destroy(query->handle);
        if (!check_call("queryDestroy", result, out_error)) return false;
        query->handle = 0;
        set_null(out_result);
        return true;
    });
}

bool codec_encode_i64(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("codecEncodeI64", args, argc, 2, out_result,
                             out_error)) return false;
        std::int64_t value = 0;
        std::size_t width = 0;
        if (!get_i64(args[0], "codec value", value, out_error) ||
            !get_codec_width(args[1], width, out_error)) return false;
        set_byte_result(static_cast<std::uint64_t>(value), width, out_result);
        return true;
    });
}

bool codec_encode_u64(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("codecEncodeU64", args, argc, 2, out_result,
                             out_error)) return false;
        std::uint64_t value = 0;
        std::size_t width = 0;
        if (!get_u64(args[0], "codec value", value, out_error) ||
            !get_codec_width(args[1], width, out_error)) return false;
        set_byte_result(value, width, out_result);
        return true;
    });
}

bool codec_decode_i64(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("codecDecodeI64", args, argc, 3, out_result,
                             out_error)) return false;
        std::uint64_t bits = 0;
        std::size_t width = 0;
        if (!read_codec_bits(args[0], args[1], args[2], bits, width,
                             out_error)) return false;
        if (width < 8 && (bits & (std::uint64_t{1} << (width * 8U - 1U))) != 0) {
            bits |= ~std::uint64_t{0} << (width * 8U);
        }
        std::int64_t value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        out_result->kind = EXPR_PACKAGE_VALUE_I64;
        out_result->as.i64_value = value;
        return true;
    });
}

bool codec_decode_u64(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("codecDecodeU64", args, argc, 3, out_result,
                             out_error)) return false;
        std::uint64_t bits = 0;
        std::size_t width = 0;
        if (!read_codec_bits(args[0], args[1], args[2], bits, width,
                             out_error)) return false;
        out_result->kind = EXPR_PACKAGE_VALUE_U64;
        out_result->as.u64_value = bits;
        return true;
    });
}

bool codec_encode_f32(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("codecEncodeF32", args, argc, 1, out_result,
                             out_error)) return false;
        double value = 0.0;
        if (!get_f64(args[0], "codec value", value, out_error)) return false;
        const float narrowed = static_cast<float>(value);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &narrowed, sizeof(bits));
        set_byte_result(bits, sizeof(bits), out_result);
        return true;
    });
}

bool codec_decode_f32(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("codecDecodeF32", args, argc, 2, out_result,
                             out_error)) return false;
        ExprPackageValue width{};
        width.kind = EXPR_PACKAGE_VALUE_U64;
        width.as.u64_value = 4;
        std::uint64_t raw_bits = 0;
        std::size_t decoded_width = 0;
        if (!read_codec_bits(args[0], args[1], width, raw_bits, decoded_width,
                             out_error)) return false;
        (void)decoded_width;
        const std::uint32_t bits = static_cast<std::uint32_t>(raw_bits);
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        out_result->kind = EXPR_PACKAGE_VALUE_F64;
        out_result->as.f64_value = static_cast<double>(value);
        return true;
    });
}

bool codec_encode_f64(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("codecEncodeF64", args, argc, 1, out_result,
                             out_error)) return false;
        double value = 0.0;
        if (!get_f64(args[0], "codec value", value, out_error)) return false;
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        set_byte_result(bits, sizeof(bits), out_result);
        return true;
    });
}

bool codec_decode_f64(const ExprHostApi*, const ExprPackageValue* args,
                      std::size_t argc, ExprPackageValue* out_result,
                      ExprPackageStringView* out_error) {
    return protect(out_error, [&] {
        if (!check_arguments("codecDecodeF64", args, argc, 2, out_result,
                             out_error)) return false;
        ExprPackageValue width{};
        width.kind = EXPR_PACKAGE_VALUE_U64;
        width.as.u64_value = 8;
        std::uint64_t bits = 0;
        std::size_t decoded_width = 0;
        if (!read_codec_bits(args[0], args[1], width, bits, decoded_width,
                             out_error)) return false;
        (void)decoded_width;
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        out_result->kind = EXPR_PACKAGE_VALUE_F64;
        out_result->as.f64_value = value;
        return true;
    });
}

constexpr ExprPackageFunctionExport kFunctions[] = {
    {"worldCreate", "fn() -> handle<github:ecs-native:WorldHandle>", 0, world_create},
    {"worldDestroy", "fn(handle<github:ecs-native:WorldHandle>) -> void", 1, world_destroy},
    {"entityCreate", "fn(handle<github:ecs-native:WorldHandle>) -> u64", 1, entity_create},
    {"entityDestroy", "fn(handle<github:ecs-native:WorldHandle>, u64) -> void", 2, entity_destroy},
    {"entityIsAlive", "fn(handle<github:ecs-native:WorldHandle>, u64) -> bool", 2, entity_is_alive},
    {"componentRegister", "fn(handle<github:ecs-native:WorldHandle>, str, u64, u64) -> u64", 4, component_register},
    {"componentAdd", "fn(handle<github:ecs-native:WorldHandle>, u64, u64, Array<u8>) -> void", 4, component_add},
    {"componentRemove", "fn(handle<github:ecs-native:WorldHandle>, u64, u64) -> void", 3, component_remove},
    {"componentHas", "fn(handle<github:ecs-native:WorldHandle>, u64, u64) -> bool", 3, component_has},
    {"componentRead", "fn(handle<github:ecs-native:WorldHandle>, u64, u64, u64) -> Array<u8>", 4, component_read},
    {"componentWrite", "fn(handle<github:ecs-native:WorldHandle>, u64, u64, Array<u8>) -> void", 4, component_write},
    {"queryCreate", "fn(handle<github:ecs-native:WorldHandle>, Array<u8>) -> handle<github:ecs-native:QueryHandle>", 2, query_create},
    {"queryNext", "fn(handle<github:ecs-native:QueryHandle>) -> bool", 1, query_next},
    {"queryEntity", "fn(handle<github:ecs-native:QueryHandle>) -> u64", 1, query_entity},
    {"queryRead", "fn(handle<github:ecs-native:QueryHandle>, u64, u64) -> Array<u8>", 3, query_read},
    {"queryWrite", "fn(handle<github:ecs-native:QueryHandle>, u64, Array<u8>) -> void", 3, query_write},
    {"queryDestroy", "fn(handle<github:ecs-native:QueryHandle>) -> void", 1, query_destroy},
    {"codecEncodeI64", "fn(i64, u64) -> Array<u8>", 2, codec_encode_i64},
    {"codecEncodeU64", "fn(u64, u64) -> Array<u8>", 2, codec_encode_u64},
    {"codecDecodeI64", "fn(Array<u8>, u64, u64) -> i64", 3, codec_decode_i64},
    {"codecDecodeU64", "fn(Array<u8>, u64, u64) -> u64", 3, codec_decode_u64},
    {"codecEncodeF32", "fn(f64) -> Array<u8>", 1, codec_encode_f32},
    {"codecDecodeF32", "fn(Array<u8>, u64) -> f64", 2, codec_decode_f32},
    {"codecEncodeF64", "fn(f64) -> Array<u8>", 1, codec_encode_f64},
    {"codecDecodeF64", "fn(Array<u8>, u64) -> f64", 2, codec_decode_f64},
};

constexpr ExprPackageRegistration kRegistration = {
    EXPR_NATIVE_PACKAGE_ABI_VERSION, "github", "ecs-native", kFunctions,
    sizeof(kFunctions) / sizeof(kFunctions[0]), nullptr, 0};

}  // namespace

extern "C" const ExprPackageRegistration* exprRegisterPackage(void) {
    return &kRegistration;
}
