#pragma once

#include <string>

#include "cerfir_block.h"

namespace cerfengine::cerfir {

bool Verify(const Block& block, std::string* error);

}
