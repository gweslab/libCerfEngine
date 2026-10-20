#pragma once

#include <cstdint>

#include "mips_ir_context.h"

namespace cerfengine {

void MipsIrBranchUncond(MipsIrContext& c, uint32_t target, uint32_t isa);
void MipsIrBranchRegister(MipsIrContext& c, uint32_t rs, bool mips16);
void MipsIrBranchCond(MipsIrContext& c, cerfir::Ref cond, uint32_t target, bool likely);
void MipsIrBranchCondTest(MipsIrContext& c, const cerfir::SlotTest& test, uint32_t target,
                          bool likely);
void MipsIrBranchDeferred(MipsIrContext& c, const cerfir::SlotTest& test, uint32_t target,
                          bool likely);

}
