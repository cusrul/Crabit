#include <NanoUI/product/core/utf8.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef NANOUI_TEST_FONT_PROVIDER
#include <NanoUI/product/hal/font_engine.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
using Decoder = uint32_t (*)(const char**);
unsigned long checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "UTF-8 contract failed: %s\n", message);
        std::exit(1);
    }
}

void expect(Decoder decode, const char* text, uint32_t scalar, size_t advance) {
    const char* cursor = text;
    require(decode(&cursor) == scalar, "scalar/replacement value");
    require(cursor == text + advance, "cursor movement");
}

size_t encode(uint32_t scalar, char* bytes) {
    size_t length = 0;
    if (scalar < 0x80) {
        bytes[length++] = static_cast<char>(scalar);
    } else if (scalar < 0x800) {
        bytes[length++] = static_cast<char>(0xC0 | (scalar >> 6));
        bytes[length++] = static_cast<char>(0x80 | (scalar & 0x3F));
    } else if (scalar < 0x10000) {
        bytes[length++] = static_cast<char>(0xE0 | (scalar >> 12));
        bytes[length++] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
        bytes[length++] = static_cast<char>(0x80 | (scalar & 0x3F));
    } else {
        bytes[length++] = static_cast<char>(0xF0 | (scalar >> 18));
        bytes[length++] = static_cast<char>(0x80 | ((scalar >> 12) & 0x3F));
        bytes[length++] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3F));
        bytes[length++] = static_cast<char>(0x80 | (scalar & 0x3F));
    }
    bytes[length] = '\0';
    return length;
}

// Put the string terminator at the last readable byte. Any lookahead beyond
// NUL faults instead of silently reading adjacent heap/string-literal storage.
class GuardedString {
    char* storage_ = nullptr;
    size_t pageSize_ = 0;

   public:
    GuardedString() {
#ifdef _WIN32
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        pageSize_ = info.dwPageSize;
        storage_ = static_cast<char*>(
            VirtualAlloc(nullptr, pageSize_ * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        require(storage_ != nullptr, "VirtualAlloc");
        DWORD oldProtection = 0;
        require(VirtualProtect(storage_ + pageSize_, pageSize_, PAGE_NOACCESS, &oldProtection),
                "VirtualProtect");
#else
        const long pageSize = sysconf(_SC_PAGESIZE);
        require(pageSize > 0, "page size");
        pageSize_ = static_cast<size_t>(pageSize);
        void* region = mmap(nullptr, pageSize_ * 2, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        require(region != MAP_FAILED, "mmap");
        storage_ = static_cast<char*>(region);
        require(mprotect(storage_ + pageSize_, pageSize_, PROT_NONE) == 0, "mprotect");
#endif
    }

    ~GuardedString() {
#ifdef _WIN32
        VirtualFree(storage_, 0, MEM_RELEASE);
#else
        munmap(storage_, pageSize_ * 2);
#endif
    }

    GuardedString(const GuardedString&) = delete;
    GuardedString& operator=(const GuardedString&) = delete;

    const char* assign(const char* text) {
        const size_t length = std::strlen(text);
        require(length + 1 <= pageSize_, "guarded input fits");
        char* begin = storage_ + pageSize_ - length - 1;
        std::memcpy(begin, text, length + 1);
        return begin;
    }
};

void runSuite(Decoder decode) {
    const char* nullCursor = nullptr;
    require(decode(nullptr) == 0, "null cursor argument");
    require(decode(&nullCursor) == 0 && nullCursor == nullptr, "null string");
    expect(decode, "", 0, 0);

    for (uint32_t scalar = 1; scalar <= 0x10FFFF; ++scalar) {
        if (scalar >= 0xD800 && scalar <= 0xDFFF) {
            continue;
        }
        char encoded[5];
        const size_t length = encode(scalar, encoded);
        expect(decode, encoded, scalar, length);
    }

    const char* malformed[] = {
        "\x80",
        "\xBF",
        "\xC0\xAF",
        "\xC1\xBF",
        "\xC2",
        "\xDF",
        "\xE0",
        "\xE4\xB8",
        "\xEF\xBF",
        "\xF0",
        "\xF0\x90",
        "\xF4\x8F\xBF",
        "\xE0\x80\x80",
        "\xED\xA0\x80",
        "\xF0\x80\x80\x80",
        "\xF4\x90\x80\x80",
        "\xF5\x80\x80\x80",
        "\xFE",
        "\xFF",
        "\xC2"
        "A",
        "\xE4\xB8"
        "A",
        "\xF0\x90\x80"
        "A",
    };
    GuardedString guarded;
    expect(decode, guarded.assign(""), 0, 0);
    for (const char* text : malformed) {
        const char* begin = guarded.assign(text);
        expect(decode, begin, 0xFFFD, 1);
        const char* cursor = begin;
        const char* terminator = begin + std::strlen(text);
        while (cursor < terminator) {
            const char* previous = cursor;
            const uint8_t byte = static_cast<uint8_t>(*previous);
            const uint32_t expected = byte < 0x80 ? byte : 0xFFFD;
            require(decode(&cursor) == expected, "every recovery byte preserves valid suffix");
            require(cursor == previous + 1 && cursor <= terminator, "one-byte error recovery");
        }
        require(decode(&cursor) == 0 && cursor == terminator, "end remains in bounds");
    }

    const char* mixed =
        "A\xE4"
        "B";
    require(decode(&mixed) == 'A', "ASCII prefix survives");
    require(decode(&mixed) == 0xFFFD, "invalid byte is replaced");
    require(decode(&mixed) == 'B', "valid suffix is not swallowed");
    require(decode(&mixed) == 0, "mixed string ends");
}
}  // namespace

int main() {
    runSuite(NanoUI::detail::decodeUtf8);
#ifdef NANOUI_TEST_FONT_PROVIDER
    runSuite(decode_utf8);
#endif
    std::printf("UTF-8 contracts passed: %lu checks, scalar roundtrips and guarded endings\n",
                checks);
}
