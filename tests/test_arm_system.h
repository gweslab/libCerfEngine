#pragma once

#include "test_host.h"

#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/engine_setup.h>

#include <cstdint>

class TestCoprocessor {
public:
    virtual ~TestCoprocessor() = default;
    virtual cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess& access) = 0;
};

class TestArmSystem : public cerfengine::ArmSystemHost {
public:
    void Attach(cerfengine::Setup& setup) {
        setup.arm_translation     = this;
        setup.arm_alignment_fault = this;
        setup.arm_system          = this;
    }

    void SetCoprocessor(TestCoprocessor* coprocessor) { coprocessor_ = coprocessor; }

    cerfengine::ArmCoprocDecision ArmCoprocessor(const cerfengine::ArmCoprocAccess& a) override {
        if (coprocessor_ == nullptr) TestFail("unexpected coprocessor access cp%u", a.cp_num);
        return coprocessor_->Decide(a);
    }
    cerfengine::ArmResetResult ArmReset() override { return {}; }

    void AllowAlignmentFaults() { allow_alignment_faults_ = true; }
    void RecordAlignmentFault(uint32_t va, bool write) {
        if (!allow_alignment_faults_) TestFail("unexpected alignment fault at 0x%08X", va);
        ++alignment_faults_;
        last_fault_va_    = va;
        last_fault_write_ = write;
    }
    uint32_t AlignmentFaults() const { return alignment_faults_; }
    uint32_t LastFaultVa() const { return last_fault_va_; }
    bool     LastFaultWrite() const { return last_fault_write_; }

    void EnableIdentityWalk(uint32_t hole_base, uint32_t hole_size) {
        walk_enabled_ = true;
        hole_base_    = hole_base;
        hole_size_    = hole_size;
    }

    void EnablePrivilegedRegion(uint32_t base, uint32_t size) {
        privileged_base_ = base;
        privileged_size_ = size;
    }

    void SetPagesGlobal(bool global) { pages_global_ = global; }

    void RemapPage(uint32_t va_page, uint32_t pa_page) {
        remap_va_ = va_page;
        remap_pa_ = pa_page;
    }

    cerfengine::ArmTranslation Walk(uint32_t mva, bool write, bool user) {
        if (!walk_enabled_) TestUnexpectedWalk(mva);
        cerfengine::ArmTranslation t;
        if (mva - hole_base_ < hole_size_ || (user && mva - privileged_base_ < privileged_size_)) {
            ++walk_faults_;
            last_walk_fault_va_    = mva;
            last_walk_fault_write_ = write;
            return t;
        }
        t.mapped = true;
        t.pa     = MappedPa(mva);
        t.global = pages_global_;
        return t;
    }

    bool Probe(uint32_t mva, uint32_t* pa) {
        if (!walk_enabled_) TestFail("unexpected ARM probe translation of 0x%08X", mva);
        if (mva - hole_base_ < hole_size_) return false;
        *pa = MappedPa(mva);
        return true;
    }

    uint32_t MappedPa(uint32_t mva) const {
        return (mva & ~0xFFFu) == remap_va_ ? remap_pa_ | (mva & 0xFFFu) : mva;
    }

    uint32_t WalkFaults() const { return walk_faults_; }
    uint32_t LastWalkFaultVa() const { return last_walk_fault_va_; }
    bool     LastWalkFaultWrite() const { return last_walk_fault_write_; }

private:
    TestCoprocessor* coprocessor_ = nullptr;

    bool     allow_alignment_faults_ = false;
    uint32_t alignment_faults_       = 0;
    uint32_t last_fault_va_          = 0;
    bool     last_fault_write_       = false;
    bool     walk_enabled_           = false;
    uint32_t hole_base_              = 0;
    uint32_t hole_size_              = 0;
    uint32_t privileged_base_        = 0;
    uint32_t privileged_size_        = 0;
    bool     pages_global_           = true;
    uint32_t remap_va_               = 0xFFFFFFFFu;
    uint32_t remap_pa_               = 0;
    uint32_t walk_faults_            = 0;
    uint32_t last_walk_fault_va_     = 0;
    bool     last_walk_fault_write_  = false;
};
