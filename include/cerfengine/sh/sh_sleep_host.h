#pragma once

namespace cerfengine {

class ShSleepHost {
public:
    virtual ~ShSleepHost() = default;

    virtual bool StandbySelected() const = 0;
    virtual void EnterStandby()          = 0;
    virtual bool StandbyExited() const   = 0;
};

}
