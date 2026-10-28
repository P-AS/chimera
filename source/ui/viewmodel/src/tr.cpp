#include "chimera/vm/tr.hpp"

#include "chimera/engine.h"

#include <vector>

namespace chimera::vm {

std::string tr(const char *key, std::initializer_list<std::string> args)
{
	return tr(key, -1, args);
}

std::string tr(const char *key, int64_t count, std::initializer_list<std::string> args)
{
	std::vector<const char *> pointers;
	for (const std::string &a : args) pointers.push_back(a.c_str());
	return ce_text(key, count, pointers.data(), static_cast<int32_t>(pointers.size()));
}

} // namespace chimera::vm
