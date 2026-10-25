#pragma once

#include <cstddef>
#include <cstdint>

namespace cerfengine::platform {

class FaultText {
public:
    FaultText(char* out, size_t size, int signal, const void* data_address);

    void Register(const char* name, uint64_t value);
    void Registers(const char* prefix, const uint64_t* values, int count);

private:
    __attribute__((format(printf, 2, 3))) void Append(const char* format, ...);

    char*  out_;
    size_t size_;
    size_t used_ = 0;
};

}
