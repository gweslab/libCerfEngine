#include "host_fault_text.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace cerfengine::platform {

FaultText::FaultText(char* out, size_t size, int signal, const void* data_address) : out_(out), size_(size) {
    Append("signal %d (%s) data=%p", signal, strsignal(signal), data_address);
}

void FaultText::Register(const char* name, uint64_t value) {
    Append(" %s=%llX", name, static_cast<unsigned long long>(value));
}

void FaultText::Registers(const char* prefix, const uint64_t* values, int count) {
    for (int i = 0; i < count; ++i) Append(" %s%d=%llX", prefix, i, static_cast<unsigned long long>(values[i]));
}

void FaultText::Append(const char* format, ...) {
    if (used_ >= size_) return;
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(out_ + used_, size_ - used_, format, args);
    va_end(args);
    used_ = written < 0 ? size_ : used_ + static_cast<size_t>(written);
}

}
