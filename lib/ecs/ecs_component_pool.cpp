#include "ecs/ecs_component_pool.hpp"

#include <cstring>
#include <stdexcept>

namespace mog::ecs {

EcsErrorCode ComponentPool::add(EcsEntityId entity,
                                const void* component_data,
                                std::uint32_t data_size) {
    if (component_data == nullptr) {
        return EcsErrorCode::InvalidArgument;
    }
    if (data_size != type_info_.size) {
        return EcsErrorCode::InvalidComponentSize;
    }

    const std::uint32_t entity_slot = entity_index(entity);
    if (entity_slot < sparse_.size() &&
        sparse_[entity_slot] != kInvalidDenseIndex) {
        return EcsErrorCode::ComponentAlreadyExists;
    }
    if (dense_entities_.size() >=
        static_cast<std::size_t>(kInvalidDenseIndex)) {
        throw std::length_error("ECS component pool index space is exhausted");
    }
    if (entity_slot >= sparse_.size()) {
        sparse_.resize(static_cast<std::size_t>(entity_slot) + 1,
                       kInvalidDenseIndex);
    }

    const std::size_t old_byte_size = dense_components_.size();
    dense_entities_.push_back(entity);
    try {
        dense_components_.resize(old_byte_size + type_info_.size);
    } catch (...) {
        dense_entities_.pop_back();
        throw;
    }

    std::memcpy(dense_components_.data() + old_byte_size, component_data,
                type_info_.size);
    sparse_[entity_slot] =
        static_cast<std::uint32_t>(dense_entities_.size() - 1);
    return EcsErrorCode::Ok;
}

bool ComponentPool::remove(EcsEntityId entity) noexcept {
    const std::uint32_t removed_index = dense_index(entity);
    if (removed_index == kInvalidDenseIndex) {
        return false;
    }

    const std::uint32_t last_index =
        static_cast<std::uint32_t>(dense_entities_.size() - 1);
    if (removed_index != last_index) {
        const EcsEntityId moved_entity = dense_entities_[last_index];
        dense_entities_[removed_index] = moved_entity;
        std::memcpy(dense_components_.data() +
                        static_cast<std::size_t>(removed_index) * type_info_.size,
                    dense_components_.data() +
                        static_cast<std::size_t>(last_index) * type_info_.size,
                    type_info_.size);
        sparse_[entity_index(moved_entity)] = removed_index;
    }

    dense_entities_.pop_back();
    dense_components_.resize(dense_components_.size() - type_info_.size);
    sparse_[entity_index(entity)] = kInvalidDenseIndex;
    return true;
}

bool ComponentPool::contains(EcsEntityId entity) const noexcept {
    return dense_index(entity) != kInvalidDenseIndex;
}

EcsErrorCode ComponentPool::read(EcsEntityId entity, void* out_data,
                                 std::uint32_t out_data_size) const noexcept {
    if (out_data == nullptr) {
        return EcsErrorCode::InvalidArgument;
    }
    if (out_data_size != type_info_.size) {
        return EcsErrorCode::InvalidComponentSize;
    }
    const std::uint32_t index = dense_index(entity);
    if (index == kInvalidDenseIndex) {
        return EcsErrorCode::ComponentNotFound;
    }

    std::memcpy(out_data,
                dense_components_.data() +
                    static_cast<std::size_t>(index) * type_info_.size,
                type_info_.size);
    return EcsErrorCode::Ok;
}

EcsErrorCode ComponentPool::write(EcsEntityId entity, const void* data,
                                  std::uint32_t data_size) noexcept {
    if (data == nullptr) {
        return EcsErrorCode::InvalidArgument;
    }
    if (data_size != type_info_.size) {
        return EcsErrorCode::InvalidComponentSize;
    }
    const std::uint32_t index = dense_index(entity);
    if (index == kInvalidDenseIndex) {
        return EcsErrorCode::ComponentNotFound;
    }

    std::memcpy(dense_components_.data() +
                    static_cast<std::size_t>(index) * type_info_.size,
                data, type_info_.size);
    return EcsErrorCode::Ok;
}

std::uint32_t ComponentPool::dense_index(EcsEntityId entity) const noexcept {
    const std::uint32_t entity_slot = entity_index(entity);
    if (entity_slot >= sparse_.size()) {
        return kInvalidDenseIndex;
    }
    const std::uint32_t index = sparse_[entity_slot];
    if (index == kInvalidDenseIndex || index >= dense_entities_.size() ||
        dense_entities_[index] != entity) {
        return kInvalidDenseIndex;
    }
    return index;
}

}  // namespace mog::ecs
