#include <cerfengine/host_request_channel.h>

#include <utility>

namespace cerfengine {

void HostRequestChannel::RegisterListener(std::function<void()> fn) {
    listeners_.push_back(std::move(fn));
}

void HostRequestChannel::ServiceRequests() {
    for (auto& fn : listeners_) fn();
}

}
