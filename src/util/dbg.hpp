#pragma once

#if __has_include(<dbg.h>)
#include <dbg.h>  // IWYU pragma: export
#else

#include <utility>

namespace dbg {
template <typename T>
T&& identity(T&& t) {
  return std::forward<T>(t);
}

template <typename T, typename... U>
auto identity(T&&, U&&... u) {
  return identity(std::forward<U>(u)...);
}
}

#define dbg(...) dbg::identity(__VA_ARGS__)

#endif
