#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include <cerfengine/calling_convention.h>
#include <cerfengine/engine.h>

#include "../../cerfir/cerfir_block.h"
#include "../../cerfir/cerfir_guest_model.h"

namespace cerfengine {

struct ArmCpuState;

class ArmCpu;
class ArmInterruptChannel;
class ArmMmu;
class ArmRoutedAccess;
class ArmRoutedInstruction;

using ArmIrSlowDesc = cerfir::MemorySlowDesc;

class ArmIrMemorySlowPath : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kArm; }
    void OnReady() override;

    cerfir::MemorySlowModel SlowModel() const {
        return cerfir::MemorySlowModel{this, entries_};
    }

private:
    static ArmIrMemorySlowPath* Owner(const ArmIrSlowDesc* desc) {
        return static_cast<ArmIrMemorySlowPath*>(const_cast<void*>(desc->context));
    }

    template <bool kStore, bool kWb>
    uint64_t CompleteAbort(const ArmIrSlowDesc& d, uint32_t wb);
    template <bool kStore, bool kWb>
    uint64_t CompleteAlign(uint32_t va, const ArmIrSlowDesc& d, uint32_t wb);
    template <bool kStore, bool kAlign>
    static uint64_t CERFENGINE_FASTCALL CompleteNoWb(uint32_t va, const ArmIrSlowDesc* desc);
    template <bool kStore, bool kAlign>
    static uint64_t CERFENGINE_FASTCALL CompleteWb(uint32_t va, const ArmIrSlowDesc* desc,
                                                   uint32_t wb);

    static constexpr uint32_t kShapes = 64;

    static constexpr uint32_t ShapeIndex(bool store, uint32_t width_index, bool sign, bool wb,
                                         bool stop) {
        return (static_cast<uint32_t>(store) << 5) | (width_index << 3) |
               (static_cast<uint32_t>(sign) << 2) | (static_cast<uint32_t>(wb) << 1) |
               static_cast<uint32_t>(stop);
    }

    using AbortFn = uint64_t(CERFENGINE_FASTCALL*)(uint32_t va, const ArmIrSlowDesc* desc,
                                                   uint32_t value, uint32_t wb);

    struct Shape {
        bool    store;
        uint8_t width;
        bool    sign;
        bool    wb;
        AbortFn abort;
    };

    template <size_t... kShape>
    void FillStubs(std::index_sequence<kShape...>);
    template <uint32_t kShape>
    static cerfir::SlowEntries StubOf();
    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    static cerfir::SlowEntries StubOfShape();

    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    uint64_t AbortAs(uint32_t va, const ArmIrSlowDesc& d, uint32_t value, uint32_t wb);

    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    static uint64_t CERFENGINE_FASTCALL Abort0(uint32_t va, const ArmIrSlowDesc* desc);
    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    static uint64_t CERFENGINE_FASTCALL Abort1(uint32_t va, const ArmIrSlowDesc* desc,
                                               uint32_t operand);
    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    static uint64_t CERFENGINE_FASTCALL Abort2(uint32_t va, const ArmIrSlowDesc* desc,
                                               uint32_t value, uint32_t wb);
    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    static uint64_t CERFENGINE_FASTCALL Align0(uint32_t va, const ArmIrSlowDesc* desc);
    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    static uint64_t CERFENGINE_FASTCALL Align1(uint32_t va, const ArmIrSlowDesc* desc,
                                               uint32_t operand);
    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    static uint64_t CERFENGINE_FASTCALL Align2(uint32_t va, const ArmIrSlowDesc* desc,
                                               uint32_t value, uint32_t wb);
    template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
    static constexpr Shape ShapeOf() {
        return Shape{kStore, kWidth, kSign, kWb, &Abort2<kStore, kWidth, kSign, kWb, kStop>};
    }

    uint64_t Access(uint32_t va, const ArmIrSlowDesc& d, uint32_t value, uint32_t wb,
                    const Shape& s);
    uint64_t Legacy(uint32_t va, const ArmIrSlowDesc& d, uint32_t value, uint32_t wb,
                    const Shape& s);
    uint64_t Cross(uint32_t va, const ArmIrSlowDesc& d, uint32_t value, uint32_t wb,
                   const Shape& s);
    uint64_t AlignmentFault(uint32_t va, const ArmIrSlowDesc& d, uint32_t value, uint32_t wb,
                            const Shape& s);
    uint64_t IoFault(const ArmIrSlowDesc& d, uint32_t wb, bool has_wb);
    uint64_t RaiseAbort(const ArmIrSlowDesc& d, uint32_t wb, bool has_wb);

    void     Writeback(const ArmIrSlowDesc& d, uint32_t wb, bool has_wb);
    void     StoreResult(const ArmIrSlowDesc& d, uint32_t value);
    uint8_t* Translate(uint32_t va, const ArmIrSlowDesc& d, bool store);
    void     StoreSlot(int32_t disp, uint32_t value);

    ArmCpuState*          state_   = nullptr;
    ArmCpu*               cpu_     = nullptr;
    ArmMmu*               mmu_     = nullptr;
    ArmRoutedAccess*      access_  = nullptr;
    ArmRoutedInstruction* routed_  = nullptr;
    ArmInterruptChannel*  channel_ = nullptr;
    cerfir::SlowEntries   entries_[cerfir::kSlowShapes]{};
};

}
