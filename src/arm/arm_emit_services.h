#pragma once

#include <cerfengine/engine.h>

namespace cerfengine {

struct ArmCpuState;

class ArmCpu;
class ArmExceptionFrame;
class ArmRoutedAccess;
class ArmInterruptChannel;
class ArmMmu;
class ArmTranslationCache;
class ArmTranslator;
class ArmUnimplementedHalt;
class ArmNeon;
class ArmNeon2RegBitcount;
class ArmNeon2RegBitwiseNot;
class ArmNeon2RegCompareZero;
class ArmNeon2RegCvtHalfSingle;
class ArmNeon2RegCvtIntFp;
class ArmNeon2RegNarrow;
class ArmNeon2RegPairwiseAddLong;
class ArmNeon2RegReciprocal;
class ArmNeon2RegReverse;
class ArmNeon2RegSatAbsNeg;
class ArmNeon2RegScalar;
class ArmNeon2RegScalarMul;
class ArmNeon2RegShuffle;
class ArmNeon2RegSwap;
class ArmNeon2RegUnaryArith;
class ArmNeon3DiffLen;
class ArmNeon3SameFpAbsCompare;
class ArmNeon3SameFpArith;
class ArmNeon3SameFpCompare;
class ArmNeon3SameFpFma;
class ArmNeon3SameFpMinMax;
class ArmNeon3SameFpMulAcc;
class ArmNeon3SameFpPairAdd;
class ArmNeon3SameFpPairMinMax;
class ArmNeon3SameFpRecipStep;
class ArmNeonOneRegImm;
class ArmNeonSat;
class ArmNeonScalarMove;
class ArmNeonShiftImm;
class ArmNeonSimd3Same;
class ArmNeonVext;
class ArmNeonVtbl;
class ArmVfp;
class ArmVfpMemory;

class ArmEmitServices : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kArm;
    }

    ArmCpuState* CpuState() { return cpu_state_; }

    ArmCpu*             Cpu()             { return cpu_; }
    ArmMmu*             Mmu()             { return mmu_; }
    ArmTranslator*      Translator()      { return translator_; }
    cerfengine::ArmSystemHost* System()   { return system_; }
    const cerfengine::ArmFeatures* ProcessorConfig() { return processor_config_; }

    ArmTranslationCache* TranslationCache() { return translation_cache_; }
    ArmInterruptChannel* InterruptChannel() { return interrupt_channel_; }
    ArmExceptionFrame*   ExceptionFrame()   { return exception_frame_; }
    ArmRoutedAccess*     RoutedAccess()     { return routed_access_; }
    ArmUnimplementedHalt* UnimplementedHalt() { return unimplemented_halt_; }

    ArmNeon*                    Neon()                   { return neon_; }
    ArmNeonSimd3Same*           Simd3Same()              { return simd3same_; }
    ArmNeonSat*                 NeonSat()                { return neon_sat_; }
    ArmNeonShiftImm*            NeonShiftImm()           { return neon_shift_imm_; }
    ArmNeonOneRegImm*           NeonOneRegImm()          { return neon_one_reg_imm_; }
    ArmNeonScalarMove*          NeonScalarMove()         { return neon_scalar_move_; }
    ArmNeonVext*                NeonVext()               { return neon_vext_; }
    ArmNeonVtbl*                NeonVtbl()               { return neon_vtbl_; }
    ArmNeon2RegScalar*          Neon2RegScalar()         { return neon_2reg_scalar_; }
    ArmNeon3DiffLen*            Neon3DiffLen()           { return neon_3difflen_; }
    ArmNeon2RegBitcount*        Neon2RegBitcount()       { return neon_2reg_bitcount_; }
    ArmNeon2RegBitwiseNot*      Neon2RegBitwiseNot()     { return neon_2reg_bitwise_not_; }
    ArmNeon2RegCompareZero*     Neon2RegCompareZero()    { return neon_2reg_compare_zero_; }
    ArmNeon2RegCvtHalfSingle*   Neon2RegCvtHalfSingle()  { return neon_2reg_cvt_half_single_; }
    ArmNeon2RegCvtIntFp*        Neon2RegCvtIntFp()       { return neon_2reg_cvt_int_fp_; }
    ArmNeon2RegNarrow*          Neon2RegNarrow()         { return neon_2reg_narrow_; }
    ArmNeon2RegPairwiseAddLong* Neon2RegPairwiseAddLong() { return neon_2reg_pairwise_add_long_; }
    ArmNeon2RegReciprocal*      Neon2RegReciprocal()     { return neon_2reg_reciprocal_; }
    ArmNeon2RegReverse*         Neon2RegReverse()        { return neon_2reg_reverse_; }
    ArmNeon2RegSatAbsNeg*       Neon2RegSatAbsNeg()      { return neon_2reg_sat_abs_neg_; }
    ArmNeon2RegScalarMul*       Neon2RegScalarMul()      { return neon_2reg_scalar_mul_; }
    ArmNeon2RegShuffle*         Neon2RegShuffle()        { return neon_2reg_shuffle_; }
    ArmNeon2RegSwap*            Neon2RegSwap()           { return neon_2reg_swap_; }
    ArmNeon2RegUnaryArith*      Neon2RegUnaryArith()     { return neon_2reg_unary_arith_; }
    ArmNeon3SameFpAbsCompare*   Neon3SameFpAbsCompare()  { return neon_3same_fp_abs_compare_; }
    ArmNeon3SameFpArith*        Neon3SameFpArith()       { return neon_3same_fp_arith_; }
    ArmNeon3SameFpCompare*      Neon3SameFpCompare()     { return neon_3same_fp_compare_; }
    ArmNeon3SameFpFma*          Neon3SameFpFma()         { return neon_3same_fp_fma_; }
    ArmNeon3SameFpMinMax*       Neon3SameFpMinMax()      { return neon_3same_fp_min_max_; }
    ArmNeon3SameFpMulAcc*       Neon3SameFpMulAcc()      { return neon_3same_fp_mul_acc_; }
    ArmNeon3SameFpPairAdd*      Neon3SameFpPairAdd()     { return neon_3same_fp_pair_add_; }
    ArmNeon3SameFpPairMinMax*   Neon3SameFpPairMinMax()  { return neon_3same_fp_pair_min_max_; }
    ArmNeon3SameFpRecipStep*    Neon3SameFpRecipStep()   { return neon_3same_fp_recip_step_; }
    ArmVfp*                     Vfp()                    { return vfp_; }
    ArmVfpMemory*               VfpMem()                 { return vfp_mem_; }

private:
    ArmCpu*                        cpu_              = nullptr;
    ArmMmu*                        mmu_              = nullptr;
    ArmTranslator*                 translator_       = nullptr;
    ArmCpuState*                   cpu_state_        = nullptr;
    cerfengine::ArmSystemHost*     system_           = nullptr;
    const cerfengine::ArmFeatures* processor_config_ = nullptr;

    ArmTranslationCache* translation_cache_ = nullptr;
    ArmInterruptChannel* interrupt_channel_ = nullptr;
    ArmExceptionFrame*   exception_frame_   = nullptr;
    ArmRoutedAccess*     routed_access_     = nullptr;
    ArmUnimplementedHalt* unimplemented_halt_ = nullptr;

    ArmNeon*                    neon_                       = nullptr;
    ArmNeonSimd3Same*           simd3same_                  = nullptr;
    ArmNeonSat*                 neon_sat_                   = nullptr;
    ArmNeonShiftImm*            neon_shift_imm_             = nullptr;
    ArmNeonOneRegImm*           neon_one_reg_imm_           = nullptr;
    ArmNeonScalarMove*          neon_scalar_move_           = nullptr;
    ArmNeonVext*                neon_vext_                  = nullptr;
    ArmNeonVtbl*                neon_vtbl_                  = nullptr;
    ArmNeon2RegScalar*          neon_2reg_scalar_           = nullptr;
    ArmNeon3DiffLen*            neon_3difflen_              = nullptr;
    ArmNeon2RegBitcount*        neon_2reg_bitcount_         = nullptr;
    ArmNeon2RegBitwiseNot*      neon_2reg_bitwise_not_      = nullptr;
    ArmNeon2RegCompareZero*     neon_2reg_compare_zero_     = nullptr;
    ArmNeon2RegCvtHalfSingle*   neon_2reg_cvt_half_single_  = nullptr;
    ArmNeon2RegCvtIntFp*        neon_2reg_cvt_int_fp_       = nullptr;
    ArmNeon2RegNarrow*          neon_2reg_narrow_           = nullptr;
    ArmNeon2RegPairwiseAddLong* neon_2reg_pairwise_add_long_ = nullptr;
    ArmNeon2RegReciprocal*      neon_2reg_reciprocal_       = nullptr;
    ArmNeon2RegReverse*         neon_2reg_reverse_          = nullptr;
    ArmNeon2RegSatAbsNeg*       neon_2reg_sat_abs_neg_      = nullptr;
    ArmNeon2RegScalarMul*       neon_2reg_scalar_mul_       = nullptr;
    ArmNeon2RegShuffle*         neon_2reg_shuffle_          = nullptr;
    ArmNeon2RegSwap*            neon_2reg_swap_             = nullptr;
    ArmNeon2RegUnaryArith*      neon_2reg_unary_arith_      = nullptr;
    ArmNeon3SameFpAbsCompare*   neon_3same_fp_abs_compare_  = nullptr;
    ArmNeon3SameFpArith*        neon_3same_fp_arith_        = nullptr;
    ArmNeon3SameFpCompare*      neon_3same_fp_compare_      = nullptr;
    ArmNeon3SameFpFma*          neon_3same_fp_fma_          = nullptr;
    ArmNeon3SameFpMinMax*       neon_3same_fp_min_max_      = nullptr;
    ArmNeon3SameFpMulAcc*       neon_3same_fp_mul_acc_      = nullptr;
    ArmNeon3SameFpPairAdd*      neon_3same_fp_pair_add_     = nullptr;
    ArmNeon3SameFpPairMinMax*   neon_3same_fp_pair_min_max_ = nullptr;
    ArmNeon3SameFpRecipStep*    neon_3same_fp_recip_step_   = nullptr;
    ArmVfp*                     vfp_                        = nullptr;
    ArmVfpMemory*               vfp_mem_                    = nullptr;
};

}
