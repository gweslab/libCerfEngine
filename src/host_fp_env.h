#pragma once

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include "host_fp_env_x86.h"
namespace cerfengine {
using HostFpEnv = HostFpEnvX86;
}
#elif defined(__aarch64__) || defined(_M_ARM64)
#include "host_fp_env_a64.h"
namespace cerfengine {
using HostFpEnv = HostFpEnvA64;
}
#else
#error "host_fp_env.h has no floating-point exception flag code for this host"
#endif
