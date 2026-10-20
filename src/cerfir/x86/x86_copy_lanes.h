#pragma once

#include <cstddef>
#include <cstdint>
#include <emmintrin.h>

#include <cerfengine/force_inline.h>

namespace cerfengine::cerfir {

template <size_t kSourceBytes>
CERFENGINE_FORCEINLINE void X86CopyLanes(uint8_t* dst, const uint8_t (&src)[kSourceBytes],
                                         size_t length) {
    static_assert(kSourceBytes % 64u == 0u, "X86CopyLanes reads whole 64-byte groups");
    for (size_t i = 0; i < length; i += 64u) {
        const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 16u));
        const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 32u));
        const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 48u));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), a);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 16u), b);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 32u), c);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 48u), d);
    }
}

}
