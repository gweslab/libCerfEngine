#include <cerfengine/host_exception.h>

#include "guest_unwind.h"
#include "host_platform.h"

#include <utility>

namespace cerfengine {

namespace {

thread_local int t_plain_raise_depth = 0;

}

PlainRaiseScope::PlainRaiseScope() { ++t_plain_raise_depth; }

PlainRaiseScope::~PlainRaiseScope() { --t_plain_raise_depth; }

void RaiseHostFailure(std::exception_ptr failure) {
#if CERFENGINE_GUARDED_DISPATCH
    if (t_plain_raise_depth == 0 && platform::InsideDispatchGuard())
        platform::ForcedUnwindToDispatch(std::move(failure));
#endif
    std::rethrow_exception(std::move(failure));
}

}
