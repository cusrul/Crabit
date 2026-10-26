#ifndef NANOUI_MEMORY_POOL_H
#define NANOUI_MEMORY_POOL_H

#include <stddef.h>

#include <new>
#include "config.h"

namespace NanoUI {

// 内存抽象接口。
class MemoryAdapter {
   public:
    virtual ~MemoryAdapter() {}
    virtual void* alloc(size_t size) = 0;
    virtual void free(void* ptr) = 0;
};

class MemoryPool {
   private:
    MemoryAdapter* adapter = nullptr;
    size_t currentWidgetBytes = 0;
    size_t currentRenderBytes = 0;
    size_t currentVectorBytes = 0;
    size_t peakTotalBytes = 0;
    MemoryPool() {}
    void (*allocationObserver)(void*, size_t, const char*) = nullptr;
    void (*releaseObserver)(void*, const char*) = nullptr;

   public:
    static MemoryPool& instance() {
        static MemoryPool inst;
        return inst;
    }

    void setAdapter(MemoryAdapter* a) { adapter = a; }
    void setObservers(void (*allocated)(void*, size_t, const char*),
                      void (*released)(void*, const char*)) {
        allocationObserver = allocated;
        releaseObserver = released;
    }

    void* allocate(size_t size, const char* tag = "UI_Object") {
        if (!adapter) {
#if NANOUI_DYNAMIC_STORAGE
            return ::operator new(size);
#else
            return nullptr;
#endif
        }
        void* ptr = adapter->alloc(size);
        if (ptr) {
            // 按 tag 累加分类占用。
            if (tag[3] == 'W')
                currentWidgetBytes += size;  // "UI_Widget"
            else if (tag[3] == 'R')
                currentRenderBytes += size;  // "UI_Render"
            else if (tag[3] == 'V')
                currentVectorBytes += size;  // "UI_Vector"

            size_t total = currentWidgetBytes + currentRenderBytes;
            if (total > peakTotalBytes)
                peakTotalBytes = total;

            if (allocationObserver)
                allocationObserver(ptr, size, tag);
        }
        return ptr;
    }

    void deallocate(void* ptr, size_t size = 0, const char* tag = "UI_Object") {
        if (!ptr)
            return;
        if (!adapter) {
#if NANOUI_DYNAMIC_STORAGE
            ::operator delete(ptr);
#endif
            return;
        }

        // 外部传入 size 时扣减对应分类占用。
        if (size > 0) {
            if (tag[3] == 'W')
                currentWidgetBytes -= size;
            else if (tag[3] == 'R')
                currentRenderBytes -= size;
            else if (tag[3] == 'V')
                currentVectorBytes -= size;
        }

        if (releaseObserver)
            releaseObserver(ptr, tag);
        adapter->free(ptr);
    }

    // 调试与计量接口。
    size_t getWidgetUsage() const { return currentWidgetBytes; }
    size_t getRenderUsage() const { return currentRenderBytes; }
    size_t getVectorUsage() const { return currentVectorBytes; }
    size_t getPeakUsage() const { return peakTotalBytes; }
};

}  // namespace NanoUI

#endif
