#pragma once

#include <utility>

namespace cerfengine {

template <class Rollback>
class UnwindRollback {
public:
    explicit UnwindRollback(Rollback rollback) : rollback_(std::move(rollback)) {}
    ~UnwindRollback() {
        if (!committed_) rollback_();
    }

    UnwindRollback(const UnwindRollback&)            = delete;
    UnwindRollback& operator=(const UnwindRollback&) = delete;

    void Commit() { committed_ = true; }

private:
    Rollback rollback_;
    bool     committed_ = false;
};

}
