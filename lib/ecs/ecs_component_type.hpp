#pragma once

#include <cstdint>
#include <string>

#include "ecs/ecs_error.hpp"

namespace mog::ecs {

using EcsComponentTypeId = std::uint32_t;

struct EcsComponentTypeInfo {
    EcsComponentTypeId id = 0;
    std::string name;
    std::uint32_t size = 0;
    std::uint32_t alignment = 0;
};

struct ComponentRegistrationResult {
    EcsErrorCode error = EcsErrorCode::InternalError;
    EcsComponentTypeId component_type = 0;

    bool ok() const noexcept { return error == EcsErrorCode::Ok; }
    explicit operator bool() const noexcept { return ok(); }
};

}  // namespace mog::ecs
