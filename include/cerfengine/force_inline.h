#pragma once

#if defined(_MSC_VER)
#define CERFENGINE_FORCEINLINE __forceinline
#define CERFENGINE_NOINLINE    __declspec(noinline)
#elif defined(__GNUC__)
#define CERFENGINE_FORCEINLINE inline __attribute__((always_inline))
#define CERFENGINE_NOINLINE    __attribute__((noinline))
#else
#define CERFENGINE_FORCEINLINE inline
#define CERFENGINE_NOINLINE
#endif
