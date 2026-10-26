#ifndef NANOUI_PREFLIGHT_H
#define NANOUI_PREFLIGHT_H

// 保存可能被 Arduino 宏覆盖的名字，供声明期使用。
#pragma push_macro("min")
#pragma push_macro("max")
#pragma push_macro("abs")
#pragma push_macro("round")
#pragma push_macro("isspace")
#pragma push_macro("isprint")
#pragma push_macro("iscntrl")
#pragma push_macro("isupper")
#pragma push_macro("islower")
#pragma push_macro("isalpha")
#pragma push_macro("isdigit")
#pragma push_macro("ispunct")
#pragma push_macro("isalnum")
#pragma push_macro("isgraph")
#pragma push_macro("isxdigit")
#pragma push_macro("rand")
#pragma push_macro("vsnprintf")
#pragma push_macro("snprintf")

#undef min
#undef max
#undef abs
#undef round
#undef isspace
#undef isprint
#undef iscntrl
#undef isupper
#undef islower
#undef isalpha
#undef isdigit
#undef isxdigit
#undef ispunct
#undef isalnum
#undef iscntrl
#undef isgraph
#undef rand
#undef vsnprintf
#undef snprintf

#include <cstdarg>
#include <cstddef>
#include <stdint.h>

extern "C" {
    uint32_t Rand(void);
    int _rtl_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
    int _rtl_snprintf(char *buf, size_t size, const char *fmt, ...);
}

namespace std {
    #ifndef NANOUI_SHIMS_DEFINED
    #define NANOUI_SHIMS_DEFINED
    using ::Rand;
    using ::_rtl_vsnprintf;
    using ::_rtl_snprintf;
    #endif
}

#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <deque>
#include <cstdio>
#include <cstdlib>

#pragma pop_macro("snprintf")
#pragma pop_macro("vsnprintf")
#pragma pop_macro("rand")
#pragma pop_macro("isxdigit")
#pragma pop_macro("isgraph")
#pragma pop_macro("isalnum")
#pragma pop_macro("ispunct")
#pragma pop_macro("isdigit")
#pragma pop_macro("isalpha")
#pragma pop_macro("islower")
#pragma pop_macro("isupper")
#pragma pop_macro("iscntrl")
#pragma pop_macro("isprint")
#pragma pop_macro("isspace")
#pragma pop_macro("round")
#pragma pop_macro("abs")
#pragma pop_macro("max")
#pragma pop_macro("min")

#endif
