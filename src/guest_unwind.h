#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

namespace cerfengine::platform {

class CodeUnwindRegistration {
public:
    CodeUnwindRegistration() = default;
    CodeUnwindRegistration(const CodeUnwindRegistration&)            = delete;
    CodeUnwindRegistration& operator=(const CodeUnwindRegistration&) = delete;

    void Register(const void* start, size_t bytes);
    void Unregister();

private:
    std::vector<uint8_t> table_;
    size_t               fde_offset_ = 0;
};

#if CERFENGINE_GUARDED_DISPATCH
[[noreturn]] void ForcedUnwindToDispatch(std::exception_ptr failure);

void RethrowHostException();
#endif

}
