#ifndef NANOUI_TEXT_H
#define NANOUI_TEXT_H

#include <stddef.h>
#include "config.h"
#if NANOUI_DYNAMIC_STORAGE
#include <string>
#endif

namespace NanoUI {

class StringView {
    const char* value = "";
    size_t length = 0;

   public:
    constexpr StringView() = default;

    StringView(const char* text) : value(text ? text : "") {
        if (text) {
            while (text[length] != '\0')
                ++length;
        }
    }

    constexpr StringView(const char* text, size_t size)
        : value(text ? text : ""), length(text ? size : 0) {}

    const char* data() const { return value; }
    size_t size() const { return length; }
    bool empty() const { return length == 0; }

    bool equals(StringView other) const {
        if (length != other.length)
            return false;
        for (size_t i = 0; i < length; ++i) {
            if (value[i] != other.value[i])
                return false;
        }
        return true;
    }
};

template <size_t Capacity>
class StaticString {
    char buffer[Capacity + 1] = {};
    size_t length = 0;

   public:
    StaticString() = default;
    StaticString(StringView text) { assign(text); }
    StaticString(const char* text) { assign(StringView(text)); }

    template <typename Text>
    StaticString(const Text& text) {
        assign(StringView(text.c_str()));
    }

    StaticString& operator=(StringView text) {
        assign(text);
        return *this;
    }

    StaticString& operator=(const char* text) {
        assign(StringView(text));
        return *this;
    }

    StaticString& operator+=(const char* text) {
        append(StringView(text));
        return *this;
    }

    bool operator==(const StaticString& other) const { return view().equals(other.view()); }
    bool operator!=(const StaticString& other) const { return !(*this == other); }

    bool assign(StringView text) {
        if (text.size() > Capacity)
            return false;
        for (size_t i = 0; i < text.size(); ++i)
            buffer[i] = text.data()[i];
        length = text.size();
        buffer[length] = '\0';
        return true;
    }

    bool append(StringView text) {
        if (length + text.size() > Capacity)
            return false;
        for (size_t i = 0; i < text.size(); ++i)
            buffer[length + i] = text.data()[i];
        length += text.size();
        buffer[length] = '\0';
        return true;
    }

    const char* c_str() const { return buffer; }
    StringView view() const { return StringView(buffer, length); }
    size_t size() const { return length; }
    constexpr size_t capacity() const { return Capacity; }
    bool empty() const { return length == 0; }
};

#if NANOUI_DYNAMIC_STORAGE
using NanoUIText = std::string;
#else
using NanoUIText = StaticString<32>;
#endif

}  // namespace NanoUI

#endif
