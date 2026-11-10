#pragma once

// Qt 5.15 calls stdext::make_checked_array_iterator. That helper is gone from
// the MSVC STL shipped with current Visual Studio toolsets. Identity iterators
// match the fallback Qt already uses on non-MSVC compilers.
// The header is force-included for every translation unit, including MinHook's
// C sources, so the C++ section stays behind __cplusplus.
#ifdef __cplusplus
#include <cstddef>

namespace stdext {

template <class T>
T* make_unchecked_array_iterator(T* iterator)
{
  return iterator;
}

template <class T>
T* make_checked_array_iterator(T* iterator, std::size_t)
{
  return iterator;
}

}  // namespace stdext
#endif
