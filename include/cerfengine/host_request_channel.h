#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "engine.h"

namespace cerfengine {

class GuestIdleWait;

class HostRequestChannel : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    void RegisterListener(std::function<void()> fn);
    void SetKick(std::function<void()> kick);
    void Request();
    void ServicePending() {
        if (pending_.load(std::memory_order_relaxed) != 0u) TakePending();
    }
    void RunListeners();

private:
    void TakePending();

    std::atomic<uint32_t>              pending_{0};
    GuestIdleWait*                     idle_ = nullptr;
    std::function<void()>              kick_;
    std::vector<std::function<void()>> listeners_;
};

}
