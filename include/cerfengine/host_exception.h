#pragma once

#include <exception>

namespace cerfengine {

class PlainRaiseScope {
public:
    PlainRaiseScope();
    ~PlainRaiseScope();
    PlainRaiseScope(const PlainRaiseScope&)            = delete;
    PlainRaiseScope& operator=(const PlainRaiseScope&) = delete;
};

[[noreturn]] void RaiseHostFailure(std::exception_ptr failure);

}
