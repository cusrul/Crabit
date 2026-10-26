#ifndef NANOUI_EMBEDDED_VECTOR_H
#define NANOUI_EMBEDDED_VECTOR_H

#include <stddef.h>
#include <stdint.h>
#include <initializer_list>
#include <new>
#include <utility>
#include "memory_pool.h"
#if NANOUI_DYNAMIC_STORAGE
#include <vector>
#endif

namespace NanoUI {

// Fixed-capacity contiguous storage for long-lived UI collections.
// It deliberately has no dependency on std::vector or the global heap.
template <typename T, size_t Capacity>
class StaticVector {
    static_assert(Capacity > 0, "StaticVector capacity must be greater than zero");

    alignas(T) unsigned char storage[sizeof(T) * Capacity];
    size_t count = 0;
    bool overflowed = false;

    T* ptr(size_t index) { return reinterpret_cast<T*>(storage) + index; }
    const T* ptr(size_t index) const { return reinterpret_cast<const T*>(storage) + index; }

   public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    StaticVector() = default;

    StaticVector(std::initializer_list<T> values) {
        for (const T& value : values) {
            if (!push_back(value))
                break;
        }
    }

    template <typename Iterator>
    StaticVector(Iterator first, Iterator last) {
        for (; first != last; ++first) {
            if (!push_back(*first))
                break;
        }
    }

    StaticVector(const StaticVector& other) {
        for (const T& value : other)
            push_back(value);
    }

    StaticVector(StaticVector&& other) noexcept {
        for (T& value : other)
            push_back(std::move(value));
        other.clear();
    }

    StaticVector& operator=(const StaticVector& other) {
        if (this == &other)
            return *this;
        clear();
        for (const T& value : other)
            push_back(value);
        return *this;
    }

    StaticVector& operator=(StaticVector&& other) noexcept {
        if (this == &other)
            return *this;
        clear();
        for (T& value : other)
            push_back(std::move(value));
        other.clear();
        return *this;
    }

    ~StaticVector() { clear(); }

    template <typename... Args>
    bool emplace_back(Args&&... args) {
        if (count >= Capacity) {
            overflowed = true;
            return false;
        }
        ::new (static_cast<void*>(ptr(count))) T(std::forward<Args>(args)...);
        ++count;
        return true;
    }

    bool push_back(const T& value) { return emplace_back(value); }
    bool push_back(T&& value) { return emplace_back(std::move(value)); }

    void clear() {
        while (count > 0) {
            --count;
            ptr(count)->~T();
        }
    }

    bool pop_back() {
        if (count == 0)
            return false;
        --count;
        ptr(count)->~T();
        return true;
    }

    size_t size() const { return count; }
    constexpr size_t capacity() const { return Capacity; }
    bool empty() const { return count == 0; }
    bool overflowedCapacity() const { return overflowed; }

    iterator begin() { return ptr(0); }
    iterator end() { return ptr(count); }
    const_iterator begin() const { return ptr(0); }
    const_iterator end() const { return ptr(count); }

    T& front() { return *ptr(0); }
    const T& front() const { return *ptr(0); }
    T& back() { return *ptr(count - 1); }
    const T& back() const { return *ptr(count - 1); }
    T* data() { return ptr(0); }
    const T* data() const { return ptr(0); }

    T& operator[](size_t index) { return *ptr(index); }
    const T& operator[](size_t index) const { return *ptr(index); }
};

// Compatibility name used by the first migration pass. The storage policy is
// now fixed-capacity; callers that need a different bound should use the
// StaticVector<T, N> name directly.
template <typename T, size_t MaxCapacity>
class PooledVector {
    static_assert(MaxCapacity > 0, "PooledVector capacity must be greater than zero");

    T* storage = nullptr;
    size_t count = 0;
    uint8_t allocated = 0;
    bool overflowed = false;
    bool allocationFailed = false;

    bool reserve(size_t required) {
        if (required <= allocated)
            return true;
        if (required > MaxCapacity) {
            overflowed = true;
            return false;
        }

        size_t next = allocated == 0 ? 4 : allocated * 2;
        if (next < required)
            next = required;
        if (next > MaxCapacity)
            next = MaxCapacity;
        T* nextStorage =
            static_cast<T*>(MemoryPool::instance().allocate(sizeof(T) * next, "UI_Vector"));
        if (!nextStorage) {
            allocationFailed = true;
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            ::new (static_cast<void*>(nextStorage + i)) T(std::move(storage[i]));
            storage[i].~T();
        }
        if (storage) {
            MemoryPool::instance().deallocate(storage, sizeof(T) * allocated, "UI_Vector");
        }
        storage = nextStorage;
        allocated = static_cast<uint8_t>(next);
        return true;
    }

   public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    PooledVector() = default;

    PooledVector(std::initializer_list<T> values) {
        for (const T& value : values) {
            if (!push_back(value))
                break;
        }
    }

    template <typename Iterator>
    PooledVector(Iterator first, Iterator last) {
        for (; first != last; ++first) {
            if (!push_back(*first))
                break;
        }
    }

    PooledVector(const PooledVector& other) {
        for (const T& value : other) {
            if (!push_back(value))
                break;
        }
        overflowed = other.overflowed;
    }

    PooledVector(PooledVector&& other) noexcept
        : storage(other.storage),
          count(other.count),
          allocated(other.allocated),
          overflowed(other.overflowed),
          allocationFailed(other.allocationFailed) {
        other.storage = nullptr;
        other.count = 0;
        other.allocated = 0;
        other.overflowed = false;
        other.allocationFailed = false;
    }

    PooledVector& operator=(const PooledVector& other) {
        if (this == &other)
            return *this;
        clear();
        releaseStorage();
        for (const T& value : other) {
            if (!push_back(value))
                break;
        }
        overflowed = other.overflowed;
        allocationFailed = other.allocationFailed;
        return *this;
    }

    PooledVector& operator=(PooledVector&& other) noexcept {
        if (this == &other)
            return *this;
        clear();
        releaseStorage();
        storage = other.storage;
        count = other.count;
        allocated = other.allocated;
        overflowed = other.overflowed;
        allocationFailed = other.allocationFailed;
        other.storage = nullptr;
        other.count = 0;
        other.allocated = 0;
        other.overflowed = false;
        other.allocationFailed = false;
        return *this;
    }

    ~PooledVector() {
        clear();
        releaseStorage();
    }

    bool push_back(const T& value) {
        if (!reserve(count + 1))
            return false;
        ::new (static_cast<void*>(storage + count)) T(value);
        ++count;
        return true;
    }

    bool push_back(T&& value) {
        if (!reserve(count + 1))
            return false;
        ::new (static_cast<void*>(storage + count)) T(std::move(value));
        ++count;
        return true;
    }

    void clear() {
        while (count > 0) {
            --count;
            storage[count].~T();
        }
    }

    bool pop_back() {
        if (count == 0)
            return false;
        --count;
        storage[count].~T();
        return true;
    }

    size_t size() const { return count; }
    constexpr size_t capacity() const { return MaxCapacity; }
    bool empty() const { return count == 0; }
    bool overflowedCapacity() const { return overflowed; }
    bool allocationFailedPermanently() const { return allocationFailed; }
    iterator begin() { return storage; }
    iterator end() { return storage ? storage + count : nullptr; }
    const_iterator begin() const { return storage; }
    const_iterator end() const { return storage ? storage + count : nullptr; }
    T& front() { return storage[0]; }
    const T& front() const { return storage[0]; }
    T& back() { return storage[count - 1]; }
    const T& back() const { return storage[count - 1]; }
    T& operator[](size_t index) { return storage[index]; }
    const T& operator[](size_t index) const { return storage[index]; }

   private:
    void releaseStorage() {
        if (storage) {
            MemoryPool::instance().deallocate(storage, sizeof(T) * allocated, "UI_Vector");
            storage = nullptr;
            allocated = 0;
        }
    }
};

#if NANOUI_DYNAMIC_STORAGE
template <typename T>
using EmbeddedVector = std::vector<T>;
#else
template <typename T>
using EmbeddedVector = PooledVector<T, 16>;
#endif

}  // namespace NanoUI

#endif
