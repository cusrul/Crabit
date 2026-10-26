#include "owned_text.h"

#include <cstring>
#include <limits>
#include <utility>

namespace NanoUI {

OwnedText::OwnedText(OwnedText&& other) noexcept
    : allocator_(other.allocator_), bytes_(other.bytes_), length_(other.length_) {
    other.bytes_ = nullptr;
    other.length_ = 0;
}

OwnedText& OwnedText::operator=(OwnedText&& other) noexcept {
    if (this != &other) {
        clear();
        allocator_ = other.allocator_;
        bytes_ = other.bytes_;
        length_ = other.length_;
        other.bytes_ = nullptr;
        other.length_ = 0;
    }
    return *this;
}

void OwnedText::clear() noexcept {
    if (bytes_) allocator_->deallocate(bytes_, length_ + 1, alignof(char), AllocationKind::resource);
    bytes_ = nullptr;
    length_ = 0;
}

TextResult OwnedText::assign(TextView text) noexcept {
    if (text.empty()) { clear(); return TextResult::ok; }
    if (text.size() == std::numeric_limits<std::size_t>::max()) return TextResult::outOfMemory;
    if (!text.data() || std::memchr(text.data(), '\0', text.size())) return TextResult::invalidInput;
    auto* next = static_cast<char*>(allocator_->allocate(text.size() + 1, alignof(char), AllocationKind::resource));
    if (!next) return TextResult::outOfMemory;
    // Copy before releasing old bytes, also when source aliases this buffer.
    std::memcpy(next, text.data(), text.size());
    next[text.size()] = '\0';
    clear();
    bytes_ = next;
    length_ = text.size();
    return TextResult::ok;
}

} // namespace NanoUI
