#pragma once

#include <NanoUI/foundation/owned.h>
#include <NanoUI/framework/types.h>

namespace NanoUI {

class BuildContext;
class State;
class RenderObject;
struct BuildResult;

using WidgetPtr = Owned<class Widget>;

class Widget {
public:
    virtual ~Widget() noexcept;
    TypeId type() const noexcept { return type_; }
    Key key() const noexcept { return key_; }
    WidgetKind kind() const noexcept { return kind_; }
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;
    Widget(Widget&&) = delete;
    Widget& operator=(Widget&&) = delete;

protected:
    Widget(TypeId type, Key key, WidgetKind kind) noexcept
        : type_(type), key_(key), kind_(kind) {}

private:
    TypeId type_;
    Key key_;
    WidgetKind kind_;
};

class LeafWidget : public Widget {
public:
    explicit LeafWidget(TypeId type, Key key = {}) noexcept
        : Widget(type, key, WidgetKind::leaf) {}
};

class StatelessWidget : public Widget {
public:
    virtual BuildResult build(BuildContext&) const noexcept = 0;

protected:
    explicit StatelessWidget(TypeId type, Key key = {}) noexcept
        : Widget(type, key, WidgetKind::stateless) {}
};

class StatefulWidget : public Widget {
public:
    virtual Owned<State> createState(Allocator&) const noexcept = 0;

protected:
    explicit StatefulWidget(TypeId type, Key key = {}) noexcept
        : Widget(type, key, WidgetKind::stateful) {}
};

// Configuration owns no render object. Its Element retains the render identity.
class RenderWidget : public Widget {
public:
    virtual Owned<RenderObject> createRenderObject(Allocator&) const noexcept = 0;
    virtual UiError updateRenderObject(RenderObject&) const noexcept = 0;
    virtual BuildResult build(BuildContext&) const noexcept;
protected:
    explicit RenderWidget(TypeId type, Key key = {}) noexcept
        : Widget(type, key, WidgetKind::rendering) {}
};

} // namespace NanoUI
