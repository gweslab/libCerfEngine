#pragma once

#include <functional>
#include <vector>

#include "engine.h"

namespace cerfengine {

class HostRequestChannel : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void RegisterListener(std::function<void()> fn);
    void Request() { Kick(); }
    void ServiceRequests();

protected:
    virtual void Kick() = 0;

private:
    std::vector<std::function<void()>> listeners_;
};

}
