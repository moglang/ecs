cmake_minimum_required(VERSION 3.16)
project(mog_ecs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
option(BUILD_BENCHMARKS "Build the ECS benchmark executable" ON)
set(MOG_EXECUTABLE "" CACHE FILEPATH
    "Optional Mog executable used for package integration tests")

set(ECS_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
)

set(ECS_SOURCES
    lib/ecs/ecs_c_api.cpp
    lib/ecs/ecs_component_pool.cpp
    lib/ecs/ecs_entity_manager.cpp
    lib/ecs/ecs_query.cpp
    lib/ecs/ecs_world.cpp
)

add_library(mog_ecs STATIC ${ECS_SOURCES})
set_target_properties(mog_ecs PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(mog_ecs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib")
target_compile_options(mog_ecs PRIVATE ${ECS_WARNINGS})

add_library(mog_ecs_package MODULE native/src/package.cpp)
target_include_directories(mog_ecs_package PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib"
)
target_link_libraries(mog_ecs_package PRIVATE mog_ecs)
target_compile_options(mog_ecs_package PRIVATE ${ECS_WARNINGS})
set_target_properties(mog_ecs_package PROPERTIES
    PREFIX ""
    OUTPUT_NAME "package"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native"
)

if(ENABLE_ASAN)
    foreach(target mog_ecs mog_ecs_package)
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address)
    endforeach()
endif()

if(ENABLE_UBSAN)
    foreach(target mog_ecs mog_ecs_package)
        target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=undefined)
    endforeach()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ecs_runtime_tests tests/ecs_runtime_tests.cpp)
    target_link_libraries(ecs_runtime_tests PRIVATE mog_ecs)
    target_compile_options(ecs_runtime_tests PRIVATE ${ECS_WARNINGS})
    if(ENABLE_ASAN)
        target_compile_options(ecs_runtime_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(ecs_runtime_tests PRIVATE -fsanitize=address)
    endif()
    if(ENABLE_UBSAN)
        target_compile_options(ecs_runtime_tests PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(ecs_runtime_tests PRIVATE -fsanitize=undefined)
    endif()
    add_test(NAME ecs_runtime_tests COMMAND ecs_runtime_tests)

    if(MOG_EXECUTABLE)
        add_test(
            NAME ecs_package_tests
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ecs_package.sh"
                    "${MOG_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}"
                    "${CMAKE_BINARY_DIR}"
        )
    endif()
endif()

if(BUILD_BENCHMARKS)
    add_executable(ecs_runtime_benchmark benchmarks/ecs_runtime_benchmark.cpp)
    target_link_libraries(ecs_runtime_benchmark PRIVATE mog_ecs)
    target_compile_options(ecs_runtime_benchmark PRIVATE ${ECS_WARNINGS})
endif()
