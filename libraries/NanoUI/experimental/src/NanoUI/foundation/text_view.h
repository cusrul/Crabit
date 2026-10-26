#pragma once

#include <cstddef>
#include <utility>

namespace NanoUI {

// A non-owning byte range. The referenced storage must outlive the view.
class TextView {
public:
    constexpr TextView() noexcept = default;

    constexpr TextView(const char* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    TextView(const char* text) noexcept : data_(text), size_(0) {
        if (data_) {
            while (data_[size_] != '\0') ++size_;
        }
    }

    // Accept standard and project-local string views without depending on a
    // particular standard-library string_view implementation.
    template<class View,
             class = decltype(std::declval<const View&>().data()),
             class = decltype(std::declval<const View&>().size())>
    TextView(const View& text) noexcept
        : data_(text.data()), size_(static_cast<std::size_t>(text.size())) {}

    constexpr const char* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

private:
    const char* data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace NanoUI
