#include "chimera/vm/bindings.hpp"

#include "chimera/vm/json.hpp"

#include <cstdint>
#include <utility>

namespace chimera::vm {

using json::Json;

namespace {

std::string trim(std::string_view s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
	return std::string(s.substr(a, b - a));
}

} // namespace

bool Bindings::parse(std::string_view json, const std::vector<std::string> &buttonNames, std::string &error)
{
	byKey_.clear();
	Json root;
	if (!json::parse(json, root, error)) return false;
	if (root.kind != Json::Kind::Object)
	{
		error = "the bindings are not a JSON object";
		return false;
	}

	/* The file has a section per controller a package declares ("AllTrollers"
	 * -> "Genesis Controller" -> "P1 Up": "Up, J1 POV0U"). The machine runs
	 * one of them: take the section that names most of its buttons. */
	const Json *best = nullptr;
	size_t bestCount = 0;
	for (const auto &[group, sections] : root.members)
	{
		if (sections.kind != Json::Kind::Object) continue;
		for (const auto &[controller, section] : sections.members)
		{
			if (section.kind != Json::Kind::Object) continue;
			size_t count = 0;
			for (const auto &[button, keys] : section.members)
				for (const auto &name : buttonNames)
					if (keys.kind == Json::Kind::String && name == button) { count++; break; }
			if (count > bestCount) { best = &section; bestCount = count; }
		}
	}
	if (best == nullptr) return true;

	for (const auto &[button, keys] : best->members)
	{
		if (keys.kind != Json::Kind::String) continue;
		int index = -1;
		for (size_t b = 0; b < buttonNames.size(); b++)
			if (buttonNames[b] == button) { index = static_cast<int>(b); break; }
		if (index < 0) continue;
		std::string_view rest = keys.text;
		while (!rest.empty())
		{
			const size_t comma = rest.find(',');
			const std::string key = trim(rest.substr(0, comma));
			if (!key.empty()) byKey_[key].push_back(index);
			if (comma == std::string_view::npos) break;
			rest.remove_prefix(comma + 1);
		}
	}
	return true;
}

const std::vector<int> &Bindings::buttonsOf(const std::string &key) const
{
	static const std::vector<int> none;
	const auto it = byKey_.find(key);
	return it == byKey_.end() ? none : it->second;
}

} // namespace chimera::vm
