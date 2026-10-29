#include "guest_unwind.h"

#include "host_platform.h"

#include <unwind.h>

#include <cstdlib>
#include <cstring>
#include <utility>

extern "C" void __register_frame(const void* table);
extern "C" void __deregister_frame(const void* table);

namespace cerfengine::platform {

namespace {

using ExceptionClass = decltype(_Unwind_Exception::exception_class);

constexpr ExceptionClass kHaltExceptionClass = 0x544C414846524543ull;

constexpr uint8_t kCfaNop          = 0x00;
constexpr uint8_t kCfaUndefined    = 0x07;
constexpr uint8_t kCfaDefCfa       = 0x0C;
constexpr uint8_t kEncodingAbsPtr  = 0x00;
constexpr uint8_t kCieVersion      = 1;
constexpr uint8_t kCodeAlignment   = 1;
constexpr uint8_t kDataAlignmentM8 = 0x78;

#if defined(__x86_64__)
constexpr uint8_t kStackColumn  = 7;
constexpr uint8_t kReturnColumn = 16;
constexpr uint8_t kCfaOffset    = 8;
#elif defined(__aarch64__)
constexpr uint8_t kStackColumn  = 31;
constexpr uint8_t kReturnColumn = 30;
constexpr uint8_t kCfaOffset    = 0;
#endif

thread_local std::exception_ptr t_pending;
thread_local _Unwind_Exception  t_unwind;

_Unwind_Reason_Code GuestFramePersonality(int, _Unwind_Action actions, ExceptionClass exception_class,
                                          _Unwind_Exception*, _Unwind_Context*) {
    if ((actions & _UA_FORCE_UNWIND) && exception_class == kHaltExceptionClass) RaiseUnwind(kHaltUnwindCode);
    return _URC_CONTINUE_UNWIND;
}

_Unwind_Reason_Code ContinueToGuestFrame(int, _Unwind_Action, ExceptionClass, _Unwind_Exception*,
                                         _Unwind_Context*, void*) {
    return _URC_NO_REASON;
}

void Append(std::vector<uint8_t>& out, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(value >> (8u * i)));
}

void CloseRecord(std::vector<uint8_t>& out, size_t record) {
    while ((out.size() - record) % sizeof(uint64_t) != 0) out.push_back(kCfaNop);
    const uint32_t length = static_cast<uint32_t>(out.size() - record - sizeof(uint32_t));
    std::memcpy(out.data() + record, &length, sizeof(length));
}

}

void CodeUnwindRegistration::Register(const void* start, size_t bytes) {
    std::vector<uint8_t>& t = table_;
    const size_t          cie = t.size();
    Append(t, 0, 4);
    Append(t, 0, 4);
    t.push_back(kCieVersion);
    for (const char c : {'z', 'P', 'R', '\0'}) t.push_back(static_cast<uint8_t>(c));
    t.push_back(kCodeAlignment);
    t.push_back(kDataAlignmentM8);
    t.push_back(kReturnColumn);
    t.push_back(1 + sizeof(uint64_t) + 1);
    t.push_back(kEncodingAbsPtr);
    Append(t, reinterpret_cast<uintptr_t>(&GuestFramePersonality), sizeof(uint64_t));
    t.push_back(kEncodingAbsPtr);
    t.push_back(kCfaDefCfa);
    t.push_back(kStackColumn);
    t.push_back(kCfaOffset);
    t.push_back(kCfaUndefined);
    t.push_back(kReturnColumn);
    CloseRecord(t, cie);

    fde_offset_ = t.size();
    Append(t, 0, 4);
    Append(t, fde_offset_ + sizeof(uint32_t) - cie, 4);
    Append(t, reinterpret_cast<uintptr_t>(start), sizeof(uint64_t));
    Append(t, bytes, sizeof(uint64_t));
    t.push_back(0);
    CloseRecord(t, fde_offset_);
    Append(t, 0, 4);

#if defined(__APPLE__) || defined(__ANDROID__)
    __register_frame(t.data() + fde_offset_);
#else
    __register_frame(t.data());
#endif
}

void CodeUnwindRegistration::Unregister() {
#if defined(__APPLE__) || defined(__ANDROID__)
    __deregister_frame(table_.data() + fde_offset_);
#else
    __deregister_frame(table_.data());
#endif
    table_.clear();
}

void ForcedUnwindToDispatch(std::exception_ptr failure) {
    t_pending = std::move(failure);
    t_unwind  = _Unwind_Exception{};
    t_unwind.exception_class = kHaltExceptionClass;
    _Unwind_ForcedUnwind(&t_unwind, &ContinueToGuestFrame, nullptr);
    std::abort();
}

void RethrowHostException() {
    if (t_pending) std::rethrow_exception(std::exchange(t_pending, nullptr));
}

}
