#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include <cerfengine/force_inline.h>

namespace cerfengine {

template <typename T>
CERFENGINE_FORCEINLINE void ZeroFill(T& object) {
    static_assert(std::is_trivially_copyable_v<T>, "ZeroFill writes the object representation");
    static_assert(sizeof(T) % 4u == 0u, "ZeroFill writes whole 16-byte blocks and 4-byte words");
    uint8_t* const bytes = reinterpret_cast<uint8_t*>(&object);
    [&]<size_t... kBlock>(std::index_sequence<kBlock...>) {
        ((void)std::memset(bytes + kBlock * 16u, 0, 16u), ...);
    }(std::make_index_sequence<sizeof(T) / 16u>{});
    uint8_t* const tail = bytes + sizeof(T) / 16u * 16u;
    [&]<size_t... kWord>(std::index_sequence<kWord...>) {
        ((void)std::memset(tail + kWord * 4u, 0, 4u), ...);
    }(std::make_index_sequence<sizeof(T) % 16u / 4u>{});
}

}
