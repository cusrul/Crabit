#pragma once

#include "allocator.h"
#include "text_view.h"

namespace NanoUI {

enum class TextResult { ok, outOfMemory, invalidInput };

// Own the UTF-8 bytes; allocator outlives this object. Length excludes NUL.
class OwnedText {
public:
    explicit OwnedText(Allocator& allocator) noexcept : allocator_(&allocator) {}
    ~OwnedText() { clear(); }
    OwnedText(const OwnedText&) = delete;
    OwnedText& operator=(const OwnedText&) = delete;
    OwnedText(OwnedText&& other) noexcept;
    OwnedText& operator=(OwnedText&& other) noexcept;
    TextResult assign(TextView text) noexcept;
    const char* c_str() const noexcept { return bytes_ ? bytes_ : ""; }
    std::size_t size() const noexcept { return length_; }
private:
    void clear() noexcept;
    Allocator* allocator_;
    char* bytes_ = nullptr;
    std::size_t length_ = 0;
};

} // namespace NanoUI
