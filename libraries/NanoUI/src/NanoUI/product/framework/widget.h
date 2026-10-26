#ifndef FRAMEWORK_WIDGET_H
#define FRAMEWORK_WIDGET_H

#include "../rendering/render_object.h"

// ==========================================
// 【核心修复】解决 Realtek SDK 宏冲突
// ==========================================
#undef min
#undef max

typedef void (*VoidCallback)();

#include "../core/memory_pool.h"
#include "../core/embedded_vector.h"

class Widget {
   public:
    // 虚析构函数保证经基类指针释放时调用到子类。
    virtual ~Widget() {}
    virtual RenderObject* createRenderObject() = 0;
    virtual void updateRenderObject(RenderObject* /*renderObject*/) {}

    // Widget 通过 MemoryPool 分配，实际存储由配置的适配器决定。
    void* operator new(size_t size) {
        return NanoUI::MemoryPool::instance().allocate(size, "UI_Widget");
    }
    void operator delete(void* ptr, size_t size) {
        NanoUI::MemoryPool::instance().deallocate(ptr, size, "UI_Widget");
    }
};

class MultiChildWidget : public Widget {
   public:
    NanoUI::EmbeddedVector<Widget*> children;
    MultiChildWidget(NanoUI::EmbeddedVector<Widget*> children)
        : children(static_cast<NanoUI::EmbeddedVector<Widget*>&&>(children)) {}
    template <typename Container>
    MultiChildWidget(const Container& source) : children(source.begin(), source.end()) {}
    MultiChildWidget(std::initializer_list<Widget*> init_list) : children(init_list) {}

    // 销毁时递归释放所有子 Widget。
    ~MultiChildWidget() override {
        for (auto c : children) {
            if (c)
                delete c;
        }
        children.clear();
    }
};

class SingleChildWidget : public Widget {
   public:
    Widget* child;
    SingleChildWidget(Widget* child) : child(child) {}

    // 销毁时释放其子 Widget。
    ~SingleChildWidget() override {
        if (child)
            delete child;
    }
};

#endif
