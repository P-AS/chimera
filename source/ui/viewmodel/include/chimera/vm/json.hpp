#pragma once
/* Just enough JSON for what packages and projects hold: read into a tree,
 * and written back. Numbers keep their text, so a value written is the value
 * read. */

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chimera::vm::json {

struct Json
{
	enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
	/* a string's text, or a number's as written */
	std::string text;
	bool boolean = false;
	std::vector<Json> items;
	std::vector<std::pair<std::string, Json>> members;

	/* a member by name, or nullptr */
	const Json *find(std::string_view name) const;
	double number() const;

	static Json string(std::string s);
	static Json number(double d);
	static Json integer(long long i);
	static Json boolean_(bool b);
};

/* false with the reason (and where) in error */
bool parse(std::string_view text, Json &out, std::string &error);
std::string write(const Json &j);
/* a string written as JSON, quotes and escapes included */
std::string quote(std::string_view s);

} // namespace chimera::vm::json
