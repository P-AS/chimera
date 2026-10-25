#include "chimera/vm/bindings.hpp"

#include "chimera/vm/json.hpp"

#include <algorithm>
#include <cmath>
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

void Bindings::setTexts(std::vector<std::string> texts)
{
	byKey_.clear();
	texts_ = std::move(texts);
	for (size_t index = 0; index < texts_.size(); index++)
	{
		std::string_view rest = texts_[index];
		while (!rest.empty())
		{
			const size_t comma = rest.find(',');
			const std::string key = trim(rest.substr(0, comma));
			if (!key.empty()) byKey_[key].push_back(static_cast<int>(index));
			if (comma == std::string_view::npos) break;
			rest.remove_prefix(comma + 1);
		}
	}
}

bool Bindings::parse(std::string_view json, const std::vector<std::string> &buttonNames, std::string &error)
{
	return parse(json, buttonNames, "AllTrollers", error);
}

bool Bindings::parse(std::string_view json, const std::vector<std::string> &buttonNames, const std::string &group, std::string &error)
{
	byKey_.clear();
	texts_.assign(buttonNames.size(), std::string());
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
	for (const auto &[name, sections] : root.members)
	{
		/* the group asked for alone: the autofire keys are not the buttons' */
		if (name != group || sections.kind != Json::Kind::Object) continue;
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

	std::vector<std::string> texts(buttonNames.size());
	for (const auto &[button, keys] : best->members)
	{
		if (keys.kind != Json::Kind::String) continue;
		for (size_t b = 0; b < buttonNames.size(); b++)
			if (buttonNames[b] == button) texts[b] = keys.text;
	}
	setTexts(std::move(texts));
	return true;
}

const std::vector<int> &Bindings::buttonsOf(const std::string &key) const
{
	static const std::vector<int> none;
	const auto it = byKey_.find(key);
	return it == byKey_.end() ? none : it->second;
}

std::vector<AnalogBind> parseAnalogBinds(std::string_view json, const std::vector<std::string> &axisNames, std::string &error)
{
	std::vector<AnalogBind> out(axisNames.size());
	Json root;
	if (!json::parse(json, root, error) || root.kind != Json::Kind::Object) return out;
	const Json *group = root.find("AllTrollersAnalog");
	if (group == nullptr || group->kind != Json::Kind::Object) return out;
	/* the controller's section that names most of the machine's analog controls */
	const Json *best = nullptr;
	size_t bestCount = 0;
	for (const auto &[controller, section] : group->members)
	{
		if (section.kind != Json::Kind::Object) continue;
		size_t count = 0;
		for (const auto &[axis, bind] : section.members)
			for (const std::string &name : axisNames)
				if (name == axis && bind.kind == Json::Kind::Object) { count++; break; }
		if (count > bestCount) { best = &section; bestCount = count; }
	}
	if (best == nullptr) return out;
	for (const auto &[axis, bind] : best->members)
		for (size_t i = 0; i < axisNames.size(); i++)
			if (axisNames[i] == axis && bind.kind == Json::Kind::Object) analogBindFrom(json::write(bind), out[i]);
	return out;
}

std::string analogBindJson(const AnalogBind &b)
{
	Json o;
	o.kind = Json::Kind::Object;
	o.members.emplace_back("Value", Json::string(b.value));
	o.members.emplace_back("Mult", Json::number(b.mult));
	o.members.emplace_back("Deadzone", Json::number(b.deadzone));
	o.members.emplace_back("ButtonBindPositive", b.positive.empty() ? Json() : Json::string(b.positive));
	o.members.emplace_back("ButtonBindNegative", b.negative.empty() ? Json() : Json::string(b.negative));
	return json::write(o);
}

bool analogBindFrom(std::string_view text, AnalogBind &out)
{
	Json o;
	std::string error;
	if (!json::parse(text, o, error) || o.kind != Json::Kind::Object) return false;
	AnalogBind b;
	if (const Json *v = o.find("Value"); v != nullptr && v->kind == Json::Kind::String) b.value = v->text;
	if (const Json *v = o.find("Mult"); v != nullptr && v->kind == Json::Kind::Number) b.mult = v->number();
	if (const Json *v = o.find("Deadzone"); v != nullptr && v->kind == Json::Kind::Number) b.deadzone = v->number();
	if (const Json *v = o.find("ButtonBindPositive"); v != nullptr && v->kind == Json::Kind::String) b.positive = v->text;
	if (const Json *v = o.find("ButtonBindNegative"); v != nullptr && v->kind == Json::Kind::String) b.negative = v->text;
	out = b;
	return true;
}

int32_t analogValue(const AnalogBind &b, int hostValue, bool positiveHeld, bool negativeHeld, int64_t min, int64_t max,
	int64_t neutral, bool &active)
{
	double value = 0;
	if (positiveHeld) value++;
	if (negativeHeld) value--;
	const bool pushed = value != 0;
	/* the host's -10000..10000, as -1..1 */
	if (!pushed) value = hostValue / 10000.0;
	/* the dead zone, and the range left scaled back up to -1..1 */
	const double dz = b.deadzone;
	if (value < -dz) value += dz;
	else if (value < dz) value = 0.0;
	else value -= dz;
	if (dz < 1.0) value /= 1.0 - dz;
	active = pushed || value != 0.0;
	/* the multiplier, then the larger side of the range around the rest value */
	value *= b.mult;
	value *= static_cast<double>(std::max(neutral - min, max - neutral));
	value += static_cast<double>(neutral);
	const double rounded = std::nearbyint(value);
	return static_cast<int32_t>(std::clamp<double>(rounded, static_cast<double>(std::min(min, max)), static_cast<double>(std::max(min, max))));
}

std::string replaceKeyNames(const std::string &keys, const std::map<std::string, std::string> &names)
{
	auto fields = [](const std::string &text, char by) {
		std::vector<std::string> out;
		size_t at = 0;
		for (size_t sep = text.find(by);; sep = text.find(by, at))
		{
			out.push_back(text.substr(at, sep == std::string::npos ? std::string::npos : sep - at));
			if (sep == std::string::npos) break;
			at = sep + 1;
		}
		return out;
	};
	auto trimmed = [](const std::string &t) {
		const size_t a = t.find_first_not_of(' '), b = t.find_last_not_of(' ');
		return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
	};
	/* each key with the spaces around it as they were: Chimera's matched
	 * none but the first of "Shift, LeftShift" for the space */
	std::vector<std::string> seen;
	std::string out;
	for (const std::string &bind : fields(keys, ','))
	{
		std::string parts;
		for (const std::string &part : fields(bind, '+'))
		{
			const std::string word = trimmed(part);
			const auto it = names.find(word);
			std::string now = part;
			if (it != names.end()) now.replace(part.find(word), word.size(), it->second);
			parts += now + "+";
		}
		parts.pop_back();
		const std::string key = trimmed(parts);
		if (!key.empty() && std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
		seen.push_back(key);
		out += (out.empty() ? "" : ",") + parts;
	}
	return out;
}

bool keyNamesFrom(std::string_view json, std::map<std::string, std::string> &out, std::string &error)
{
	Json all;
	if (!json::parse(json, all, error)) return false;
	if (all.kind != Json::Kind::Object)
	{
		error = "the names to replace are not a JSON object";
		return false;
	}
	out.clear();
	for (const auto &[from, to] : all.members)
		if (to.kind == Json::Kind::String) out[from] = to.text;
	return true;
}

} // namespace chimera::vm
