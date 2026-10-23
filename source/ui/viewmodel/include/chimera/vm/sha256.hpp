#pragma once
/* SHA-256 of bytes, as upper-case hex: what Chimera's memory.hash_region answers */

#include <string>
#include <string_view>

namespace chimera::vm {

std::string sha256Hex(std::string_view bytes);

} // namespace chimera::vm
