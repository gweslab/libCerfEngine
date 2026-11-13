#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace cerfengine {

template <class... Args>
class CallbackList {
public:
    using Callback = std::function<void(Args...)>;

    void Add(Callback callback) { callbacks_.push_back(std::move(callback)); }

    void Call(Args... args) const {
        for (const Callback& callback : callbacks_) callback(args...);
    }

    bool        Empty() const { return callbacks_.empty(); }
    std::size_t Size() const { return callbacks_.size(); }

private:
    std::vector<Callback> callbacks_;
};

}
