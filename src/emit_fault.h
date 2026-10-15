#pragma once

#include <stdexcept>

namespace cerfengine {

class EmitFault : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}
