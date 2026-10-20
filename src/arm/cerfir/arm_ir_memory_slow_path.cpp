#include "arm_ir_memory_slow_path.h"

#include <cstring>

#include <cerfengine/byte_order.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/cpu_state.h>

#include <cerfengine/force_inline.h>
#include "../arm_routed_access.h"
#include "../arm_routed_instruction.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmIrMemorySlowPath);

namespace {

constexpr uint64_t kExit = 1u;

CERFENGINE_FORCEINLINE uint64_t Continue(uint32_t value) {
    return static_cast<uint64_t>(value) << 32;
}

CERFENGINE_FORCEINLINE uint32_t Extend(uint32_t value, uint8_t width, bool sign) {
    if (!sign) return value;
    if (width == 1u) return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(value)));
    if (width == 2u) return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
    return value;
}

}

CERFENGINE_FORCEINLINE void ArmIrMemorySlowPath::StoreSlot(int32_t disp, uint32_t value) {
    std::memcpy(reinterpret_cast<uint8_t*>(state_) + disp, &value, sizeof(value));
}

CERFENGINE_FORCEINLINE void ArmIrMemorySlowPath::Writeback(const ArmIrSlowDesc& d, uint32_t wb,
                                                           bool has_wb) {
    if (has_wb) StoreSlot(d.writeback_slot, wb + d.wb_add);
}

CERFENGINE_FORCEINLINE void ArmIrMemorySlowPath::StoreResult(const ArmIrSlowDesc& d,
                                                             uint32_t value) {
    if (d.result_slot != cerfir::kNoSlot) StoreSlot(d.result_slot, value);
}

template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
CERFENGINE_FORCEINLINE uint64_t ArmIrMemorySlowPath::AbortAs(uint32_t va, const ArmIrSlowDesc& d,
                                                             uint32_t value, uint32_t wb) {
    constexpr MmioWidth kMmio = static_cast<MmioWidth>(kWidth);
    if (!mmu_->io_pending()) return RaiseAbort(d, wb, kWb);
    if (channel_->BackOutForIrq(d.guest_pc)) {
        mmu_->ClearIoPending();
        return kExit;
    }
    if constexpr (kStore) {
        ArmRoutedAccess::IoStoreHelper<kMmio>(access_, d.guest_pc, va, value);
    } else {
        value = Extend(ArmRoutedAccess::IoLoadHelper<kMmio>(access_, d.guest_pc, va), kWidth, kSign);
    }
    if constexpr (kWb) StoreSlot(d.writeback_slot, wb + d.wb_add);
    if constexpr (kStop) {
        if (channel_->LeaveAfterAccess(d.next_pc)) {
            if constexpr (!kStore) StoreResult(d, value);
            return kExit;
        }
    }
    return Continue(value);
}

template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
uint64_t CERFENGINE_FASTCALL ArmIrMemorySlowPath::Abort0(uint32_t va, const ArmIrSlowDesc* desc) {
    return Owner(desc)->AbortAs<kStore, kWidth, kSign, kWb, kStop>(va, *desc, 0u, 0u);
}

template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
uint64_t CERFENGINE_FASTCALL ArmIrMemorySlowPath::Abort1(uint32_t va, const ArmIrSlowDesc* desc,
                                                         uint32_t operand) {
    if constexpr (kStore) {
        return Owner(desc)->AbortAs<kStore, kWidth, kSign, kWb, kStop>(va, *desc, operand, 0u);
    } else {
        return Owner(desc)->AbortAs<kStore, kWidth, kSign, kWb, kStop>(va, *desc, 0u, operand);
    }
}

template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
uint64_t CERFENGINE_FASTCALL ArmIrMemorySlowPath::Abort2(uint32_t va, const ArmIrSlowDesc* desc,
                                                         uint32_t value, uint32_t wb) {
    return Owner(desc)->AbortAs<kStore, kWidth, kSign, kWb, kStop>(va, *desc, value, wb);
}

template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
uint64_t CERFENGINE_FASTCALL ArmIrMemorySlowPath::Align0(uint32_t va, const ArmIrSlowDesc* desc) {
    constexpr Shape kShape = ShapeOf<kStore, kWidth, kSign, kWb, kStop>();
    return Owner(desc)->Access(va, *desc, 0u, 0u, kShape);
}

template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
uint64_t CERFENGINE_FASTCALL ArmIrMemorySlowPath::Align1(uint32_t va, const ArmIrSlowDesc* desc,
                                                         uint32_t operand) {
    constexpr Shape kShape = ShapeOf<kStore, kWidth, kSign, kWb, kStop>();
    if constexpr (kStore) {
        return Owner(desc)->Access(va, *desc, operand, 0u, kShape);
    } else {
        return Owner(desc)->Access(va, *desc, 0u, operand, kShape);
    }
}

template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
uint64_t CERFENGINE_FASTCALL ArmIrMemorySlowPath::Align2(uint32_t va, const ArmIrSlowDesc* desc,
                                                         uint32_t value, uint32_t wb) {
    constexpr Shape kShape = ShapeOf<kStore, kWidth, kSign, kWb, kStop>();
    return Owner(desc)->Access(va, *desc, value, wb, kShape);
}

template <bool kStore, uint8_t kWidth, bool kSign, bool kWb, bool kStop>
cerfir::SlowEntries ArmIrMemorySlowPath::StubOfShape() {
    if constexpr (kStore && kWb) {
        return cerfir::SlowEntries{
            reinterpret_cast<const void*>(&Abort2<kStore, kWidth, kSign, kWb, kStop>),
            reinterpret_cast<const void*>(&Align2<kStore, kWidth, kSign, kWb, kStop>)};
    } else if constexpr (kStore || kWb) {
        return cerfir::SlowEntries{
            reinterpret_cast<const void*>(&Abort1<kStore, kWidth, kSign, kWb, kStop>),
            reinterpret_cast<const void*>(&Align1<kStore, kWidth, kSign, kWb, kStop>)};
    } else {
        return cerfir::SlowEntries{
            reinterpret_cast<const void*>(&Abort0<kStore, kWidth, kSign, kWb, kStop>),
            reinterpret_cast<const void*>(&Align0<kStore, kWidth, kSign, kWb, kStop>)};
    }
}

template <uint32_t kShape>
cerfir::SlowEntries ArmIrMemorySlowPath::StubOf() {
    constexpr bool     kStop       = (kShape & 1u) != 0u;
    constexpr bool     kWb         = (kShape & 2u) != 0u;
    constexpr bool     kSignBit    = (kShape & 4u) != 0u;
    constexpr uint32_t kWidthIndex = (kShape >> 3) & 3u;
    constexpr bool     kStore      = (kShape >> 5) != 0u;
    static_assert(ShapeIndex(kStore, kWidthIndex, kSignBit, kWb, kStop) == kShape);
    if constexpr (kWidthIndex == 3u) {
        return cerfir::SlowEntries{};
    } else {
        return StubOfShape<kStore, static_cast<uint8_t>(1u << kWidthIndex), !kStore && kSignBit,
                           kWb, kStop>();
    }
}

template <size_t... kShape>
void ArmIrMemorySlowPath::FillStubs(std::index_sequence<kShape...>) {
    ((entries_[kShape] = StubOf<static_cast<uint32_t>(kShape)>()), ...);
}

template <bool kStore, bool kWb>
uint64_t ArmIrMemorySlowPath::CompleteAbort(const ArmIrSlowDesc& d, uint32_t wb) {
    if (d.back_out && mmu_->io_pending() && channel_->BackOutForIrq(d.guest_pc)) {
        mmu_->ClearIoPending();
        return kExit;
    }
    if constexpr (kWb) {
        if (d.writeback_on_fault && !mmu_->io_pending()) Writeback(d, wb, true);
    }
    routed_->CompleteOrRaiseAbort(d.guest_pc, d.raw);
    return kExit;
}

template <bool kStore, bool kWb>
uint64_t ArmIrMemorySlowPath::CompleteAlign(uint32_t va, const ArmIrSlowDesc& d, uint32_t wb) {
    if constexpr (kStore) {
        ArmMmu::AlignmentFaultWriteHelper(va, mmu_);
    } else {
        ArmMmu::AlignmentFaultReadHelper(va, mmu_);
    }
    if constexpr (kWb) {
        if (d.writeback_on_fault) {
            Writeback(d, wb, true);
            routed_->CompleteOrRaiseAbort(d.guest_pc, d.raw);
            return kExit;
        }
    }
    return CompleteAbort<kStore, kWb>(d, wb);
}

template <bool kStore, bool kAlign>
uint64_t CERFENGINE_FASTCALL ArmIrMemorySlowPath::CompleteNoWb(uint32_t va,
                                                               const ArmIrSlowDesc* desc) {
    if constexpr (kAlign) {
        return Owner(desc)->CompleteAlign<kStore, false>(va, *desc, 0u);
    } else {
        return Owner(desc)->CompleteAbort<kStore, false>(*desc, 0u);
    }
}

template <bool kStore, bool kAlign>
uint64_t CERFENGINE_FASTCALL ArmIrMemorySlowPath::CompleteWb(uint32_t va, const ArmIrSlowDesc* desc,
                                                             uint32_t wb) {
    if constexpr (kAlign) {
        return Owner(desc)->CompleteAlign<kStore, true>(va, *desc, wb);
    } else {
        return Owner(desc)->CompleteAbort<kStore, true>(*desc, wb);
    }
}

void ArmIrMemorySlowPath::OnReady() {
    cpu_     = &engine_.Get<ArmCpu>();
    state_   = cpu_->State();
    mmu_     = &engine_.Get<ArmMmu>();
    access_  = &engine_.Get<ArmRoutedAccess>();
    routed_  = &engine_.Get<ArmRoutedInstruction>();
    channel_ = &engine_.Get<ArmInterruptChannel>();
    FillStubs(std::make_index_sequence<kShapes>{});
    using cerfir::SlowShapeIndex;
    entries_[SlowShapeIndex(true, false, 4u, false, false, false)] = cerfir::SlowEntries{
        reinterpret_cast<const void*>(&CompleteNoWb<false, false>),
        reinterpret_cast<const void*>(&CompleteNoWb<false, true>)};
    entries_[SlowShapeIndex(true, false, 4u, false, true, false)] = cerfir::SlowEntries{
        reinterpret_cast<const void*>(&CompleteWb<false, false>),
        reinterpret_cast<const void*>(&CompleteWb<false, true>)};
    entries_[SlowShapeIndex(true, true, 4u, false, false, false)] = cerfir::SlowEntries{
        reinterpret_cast<const void*>(&CompleteNoWb<true, false>),
        reinterpret_cast<const void*>(&CompleteNoWb<true, true>)};
    entries_[SlowShapeIndex(true, true, 4u, false, true, false)] = cerfir::SlowEntries{
        reinterpret_cast<const void*>(&CompleteWb<true, false>),
        reinterpret_cast<const void*>(&CompleteWb<true, true>)};
}

uint64_t ArmIrMemorySlowPath::Access(uint32_t va, const ArmIrSlowDesc& d, uint32_t value,
                                     uint32_t wb, const Shape& s) {
    using cerfir::AlignPolicy;
    switch (d.align) {
    case AlignPolicy::kNone:
        break;
    case AlignPolicy::kFault:
        if ((va & (s.width == 2u ? 1u : 3u)) != 0u) return AlignmentFault(va, d, value, wb, s);
        break;
    case AlignPolicy::kLegacyRotate:
        if ((va & 3u) != 0u) return Legacy(va, d, value, wb, s);
        break;
    case AlignPolicy::kUndefinedIfUnaligned:
        if ((va & 3u) != 0u) {
            ArmCpu::RaiseUndefinedExceptionHelper(cpu_, d.guest_pc);
            return kExit;
        }
        break;
    case AlignPolicy::kCrossCheck: {
        const bool crosses = s.width == 2u ? (va & 0x3FFu) == 0x3FFu
                                           : (((va & 0x3FFu) + 3u) & 0x400u) != 0u;
        if (crosses) return Cross(va, d, value, wb, s);
        break;
    }
    case AlignPolicy::kIgnoreLowBits:
        engine_.Panic("CerfIR ARM memory slow path: a single access with a block-transfer "
                      "alignment policy at guest PC 0x%08X", d.guest_pc);
    }
    return s.abort(va, &d, value, wb);
}

uint64_t ArmIrMemorySlowPath::Legacy(uint32_t va, const ArmIrSlowDesc& d, uint32_t value,
                                     uint32_t wb, const Shape& s) {
    const uint32_t aligned = va & 0xFFFFFFFCu;
    uint8_t*       host    = Translate(aligned, d, s.store);
    if (s.store) {
        if (host == nullptr) return s.abort(aligned, &d, value, wb);
        le::Put32(host, value);
        Writeback(d, wb, s.wb);
        return Continue(value);
    }
    if (host == nullptr) return IoFault(d, wb, s.wb);
    const uint32_t word   = le::U32(host);
    const uint32_t rotate = (va & 3u) * 8u;
    Writeback(d, wb, s.wb);
    return Continue((word >> rotate) | (word << (32u - rotate)));
}

uint64_t ArmIrMemorySlowPath::Cross(uint32_t va, const ArmIrSlowDesc& d, uint32_t value,
                                    uint32_t wb, const Shape& s) {
    const uint32_t user = d.unpriv ? 1u : 0u;
    if (s.store) {
        const uint32_t result = s.width == 2u
                                    ? ArmMmu::UnalignedHalfwordStoreHelper(mmu_, va, value, user)
                                    : ArmMmu::UnalignedWordStoreHelper(mmu_, va, value, user);
        if (result == 0xFFFFFFFFu) return RaiseAbort(d, wb, s.wb);
        Writeback(d, wb, s.wb);
        return Continue(value);
    }
    if (s.width == 2u) {
        const uint32_t half = ArmMmu::UnalignedHalfwordLoadHelper(mmu_, va, user);
        if (half == 0xFFFFFFFFu) return IoFault(d, wb, s.wb);
        Writeback(d, wb, s.wb);
        return Continue(Extend(half, s.width, s.sign));
    }
    const uint64_t word = ArmMmu::UnalignedWordLoadHelper(mmu_, va, user);
    if ((word >> 32) == 0u) return IoFault(d, wb, s.wb);
    Writeback(d, wb, s.wb);
    return Continue(static_cast<uint32_t>(word));
}

uint64_t ArmIrMemorySlowPath::AlignmentFault(uint32_t va, const ArmIrSlowDesc& d, uint32_t value,
                                             uint32_t wb, const Shape& s) {
    if (s.store) {
        ArmMmu::AlignmentFaultWriteHelper(va, mmu_);
    } else {
        ArmMmu::AlignmentFaultReadHelper(va, mmu_);
    }
    if (d.writeback_on_fault && s.wb) {
        Writeback(d, wb, true);
        routed_->CompleteOrRaiseAbort(d.guest_pc, d.raw);
        return kExit;
    }
    return s.abort(va, &d, value, wb);
}

uint64_t ArmIrMemorySlowPath::IoFault(const ArmIrSlowDesc& d, uint32_t wb, bool has_wb) {
    if (mmu_->io_pending() && channel_->BackOutForIrq(d.guest_pc)) {
        mmu_->ClearIoPending();
        return kExit;
    }
    return RaiseAbort(d, wb, has_wb);
}

CERFENGINE_NOINLINE uint64_t ArmIrMemorySlowPath::RaiseAbort(const ArmIrSlowDesc& d, uint32_t wb,
                                                             bool has_wb) {
    if (d.writeback_on_fault && !mmu_->io_pending()) Writeback(d, wb, has_wb);
    routed_->CompleteOrRaiseAbort(d.guest_pc, d.raw);
    return kExit;
}

uint8_t* ArmIrMemorySlowPath::Translate(uint32_t va, const ArmIrSlowDesc& d, bool store) {
    if (d.unpriv) {
        return store ? ArmMmu::TranslateUserWriteHelper(va, mmu_)
                     : ArmMmu::TranslateUserReadHelper(va, mmu_);
    }
    return store ? ArmMmu::TranslateWriteHelper(va, mmu_) : ArmMmu::TranslateReadHelper(va, mmu_);
}

}
