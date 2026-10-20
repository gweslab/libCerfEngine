#pragma once

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include "host_fp_env_x86.h"
namespace cerfengine {
using HostFpEnv = HostFpEnvX86;
}
#else
#error "host_fp_env.h has no floating-point exception flag code for this host"
#endif
