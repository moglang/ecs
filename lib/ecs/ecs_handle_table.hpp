#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mog::ecs {

template <typename T>
class HandleTable {
public:
    std::uint64_t insert(std::unique_ptr<T> object) {
        if (object == nullptr) {
            throw std::invalid_argument("cannot insert a null handle object");
        }

        std::uint32_t index = 0;
        if (!free_slots_.empty()) {
            index = free_slots_.back();
            free_slots_.pop_back();
            Slot& slot = slots_[index];
            ++slot.generation;
            if (slot.generation == 0) {
                slot.generation = 1;
            }
            slot.object = std::move(object);
        } else {
            if (slots_.size() > static_cast<std::size_t>(
                                    std::numeric_limits<std::uint32_t>::max())) {
                throw std::length_error("native handle table is exhausted");
            }
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back({std::move(object), 1});
        }
        return pack(index, slots_[index].generation);
    }

    T* get(std::uint64_t handle) noexcept {
        const std::uint32_t index = handle_index(handle);
        if (index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[index];
        return slot.object != nullptr && slot.generation == handle_generation(handle)
                   ? slot.object.get()
                   : nullptr;
    }

    const T* get(std::uint64_t handle) const noexcept {
        return const_cast<HandleTable*>(this)->get(handle);
    }

    bool remove(std::uint64_t handle) {
        const std::uint32_t index = handle_index(handle);
        if (get(handle) == nullptr) {
            return false;
        }
        free_slots_.push_back(index);
        slots_[index].object.reset();
        return true;
    }

    template <typename Predicate>
    void remove_if(Predicate predicate) {
        for (std::size_t slot_index = 0; slot_index < slots_.size();
             ++slot_index) {
            const auto index = static_cast<std::uint32_t>(slot_index);
            Slot& slot = slots_[index];
            if (slot.object != nullptr && predicate(*slot.object)) {
                free_slots_.push_back(index);
                slot.object.reset();
            }
        }
    }

private:
    struct Slot {
        std::unique_ptr<T> object;
        std::uint32_t generation;
    };

    static std::uint64_t pack(std::uint32_t index,
                              std::uint32_t generation) noexcept {
        return (static_cast<std::uint64_t>(generation) << 32U) | index;
    }
    static std::uint32_t handle_index(std::uint64_t handle) noexcept {
        return static_cast<std::uint32_t>(handle);
    }
    static std::uint32_t handle_generation(std::uint64_t handle) noexcept {
        return static_cast<std::uint32_t>(handle >> 32U);
    }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_slots_;
};

}  // namespace mog::ecs
