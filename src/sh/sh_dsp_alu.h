#pragma once

#include <cstdint>

#include <cerfengine/engine.h>

#include "sh_dsp_arithmetic.h"
#include "sh_dsp_encoding.h"

namespace cerfengine {

struct ShCpuState;

struct ShDspWrite {
    bool     specified;
    bool     active;
    ShDspReg reg;
    int64_t  value;
};

struct ShDspOutcome {
    ShDspWrite alu;
    ShDspWrite multiply;
    bool       dsr_write;
    uint32_t   dsr;
    bool       mac_write;
    bool       mac_high;
    uint32_t   mac_value;
};

class ShDspAlu : public EngineService {
public:
    using EngineService::EngineService;

    bool ShouldRegister() override { return engine_.GetSetup().isa == Isa::kSh; }
    void OnReady() override;

    ShDspOutcome Evaluate(uint32_t field_b, ShDspFieldB operation) const;
    void         Apply(const ShDspOutcome& outcome);

private:
    struct Flags {
        bool carry;
        bool negative;
        bool zero;
        bool overflow;
        bool greater;
        bool greater_equal;
    };

    static Flags ArithmeticFlags(const ShDspResult& r);

    uint32_t& Slot(ShDspReg reg) const;
    uint32_t  Word(ShDspReg reg) const;
    bool      Saturating() const;
    uint32_t  DcBit() const;
    uint32_t* Guard(ShDspReg reg) const;
    int64_t   Source(ShDspReg reg) const;
    void      Write(ShDspReg reg, int64_t value);
    uint32_t  Dsr(const Flags& f, bool dc_is_carry) const;
    int32_t   ShiftAmount(uint32_t field_b, bool immediate, uint32_t bits, int32_t limit) const;

    void Arithmetic(ShDspOutcome& o, ShDspResult r, bool dc_is_carry, bool integer) const;
    void Shift(ShDspOutcome& o, ShDspResult r) const;
    void Logical(ShDspOutcome& o, uint32_t word, bool carry) const;
    void Execute(ShDspOutcome& o, uint32_t field_b, ShDspFieldB f) const;

    ShCpuState* state_ = nullptr;
};

}
