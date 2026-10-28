#pragma once
/* A text of Chimera's language tables (docs/translations.md): the words of a
 * key in the language in use, with %1 to %9 filled by args and, for a key
 * with plural forms, the form count takes, its %n the count. Every text a
 * view model gives a person comes from here. */

#include <cstdint>
#include <initializer_list>
#include <string>

namespace chimera::vm {

std::string tr(const char *key, std::initializer_list<std::string> args = {});
std::string tr(const char *key, int64_t count, std::initializer_list<std::string> args = {});

} // namespace chimera::vm
