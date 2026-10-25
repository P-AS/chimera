#include "chimera/vm/settings_form.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "chimera/engine.h"

namespace chimera::vm {

using json::Json;

namespace {
void setMember(Json &obj, const std::string &name, Json v)
{
	obj.kind = Json::Kind::Object;
	for (auto &[k, old] : obj.members)
		if (k == name)
		{
			old = std::move(v);
			return;
		}
	obj.members.emplace_back(name, std::move(v));
}

std::string joined(const Json *list, const Json *labels)
{
	std::string out;
	if (list == nullptr) return out;
	for (size_t i = 0; i < list->items.size(); i++)
	{
		const std::string option = list->items[i].kind == Json::Kind::String ? list->items[i].text : json::write(list->items[i]);
		std::string shown = option;
		if (labels != nullptr)
			if (const Json *l = labels->find(option); l != nullptr && l->kind == Json::Kind::String) shown = l->text;
		out += (i ? "\n" : "") + shown;
	}
	return out;
}

/* Chimera's whole-number types hold what their C types hold (issue 358);
 * 128 bits here, so a ulong's and a long's bounds compare exactly */
using Whole = __int128;
constexpr int64_t kFirstSecond = -62135596800LL, kLastSecond = 253402300799LL;

bool wholeRange(const std::string &type, Whole &lo, Whole &hi)
{
	if (type == "int") lo = INT32_MIN, hi = INT32_MAX;
	else if (type == "uint") lo = 0, hi = UINT32_MAX;
	else if (type == "long") lo = INT64_MIN, hi = INT64_MAX;
	else if (type == "ulong") lo = 0, hi = static_cast<Whole>(UINT64_MAX);
	else if (type == "datetime") lo = kFirstSecond, hi = kLastSecond;
	else return false;
	return true;
}

/* a whole number written in decimal, nothing else in it */
std::optional<Whole> wholeOfText(std::string s)
{
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
	bool negative = false;
	size_t i = 0;
	if (i < s.size() && (s[i] == '-' || s[i] == '+')) negative = s[i++] == '-';
	if (i >= s.size() || s.size() - i > 20) return std::nullopt;
	Whole n = 0;
	for (; i < s.size(); i++)
	{
		if (s[i] < '0' || s[i] > '9') return std::nullopt;
		n = n * 10 + (s[i] - '0');
	}
	return negative ? -n : n;
}

std::optional<Whole> wholeOf(const Value &v)
{
	if (const int64_t *i = std::get_if<int64_t>(&v)) return *i;
	if (const double *d = std::get_if<double>(&v))
		return std::floor(*d) == *d && std::fabs(*d) < 1.8e19 ? std::optional<Whole>(static_cast<Whole>(*d)) : std::nullopt;
	if (const std::string *s = std::get_if<std::string>(&v)) return wholeOfText(*s);
	return std::nullopt;
}

std::string wholeText(Whole n)
{
	if (n == 0) return "0";
	const bool negative = n < 0;
	std::string out;
	for (Whole m = negative ? -n : n; m > 0; m /= 10) out.insert(out.begin(), static_cast<char>('0' + static_cast<int>(m % 10)));
	return negative ? "-" + out : out;
}

/* the days since 1970-01-01 of a day of the proleptic Gregorian calendar, and back */
int64_t daysFromCivil(int64_t y, int64_t m, int64_t d)
{
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const int64_t yoe = y - era * 400;
	const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

void civilFromDays(int64_t z, int64_t &y, int64_t &m, int64_t &d)
{
	z += 719468;
	const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	const int64_t doe = z - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const int64_t mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp + (mp < 10 ? 3 : -9);
	y = yoe + era * 400 + (m <= 2);
}
} // namespace

std::string SettingsForm::typeOf(const Json &decl)
{
	if (const Json *t = decl.find("type"); t != nullptr && t->kind == Json::Kind::String && !t->text.empty())
	{
		std::string lower = t->text;
		for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return lower;
	}
	if (const Json *o = decl.find("options"); o != nullptr && !o->items.empty()) return "enum";
	const Json *d = decl.find("default");
	if (d == nullptr) return "string";
	if (d->kind == Json::Kind::Bool) return "bool";
	if (d->kind == Json::Kind::Number)
	{
		const std::optional<Whole> n = wholeOfText(d->text);
		if (!n) return "float";
		if (*n >= INT32_MIN && *n <= INT32_MAX) return "int";
		return *n >= INT64_MIN && *n <= INT64_MAX ? "long" : "ulong";
	}
	return "string";
}

std::string SettingsForm::dateText(int64_t seconds)
{
	const int64_t days = seconds >= 0 ? seconds / 86400 : -((-seconds + 86399) / 86400);
	const int64_t rest = seconds - days * 86400;
	int64_t y = 0, m = 0, d = 0;
	civilFromDays(days, y, m, d);
	char text[40];
	std::snprintf(text, sizeof text, "%04lld-%02lld-%02lld %02lld:%02lld:%02lld", static_cast<long long>(y), static_cast<long long>(m),
		static_cast<long long>(d), static_cast<long long>(rest / 3600), static_cast<long long>(rest / 60 % 60), static_cast<long long>(rest % 60));
	return text;
}

std::optional<int64_t> SettingsForm::parseDate(const std::string &text)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, used = 0;
	const char *t = text.c_str();
	while (std::isspace(static_cast<unsigned char>(*t))) t++;
	/* the shown form, or without the seconds, or a date alone, and nothing after it */
	const bool whole = std::sscanf(t, "%4d-%2d-%2d %2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &s, &used) == 6;
	if (!whole)
	{
		s = 0;
		if (std::sscanf(t, "%4d-%2d-%2d %2d:%2d%n", &y, &mo, &d, &h, &mi, &used) != 5)
		{
			h = mi = 0;
			if (std::sscanf(t, "%4d-%2d-%2d%n", &y, &mo, &d, &used) != 3) return std::nullopt;
		}
	}
	for (const char *rest = t + used; *rest != '\0'; rest++)
		if (!std::isspace(static_cast<unsigned char>(*rest))) return std::nullopt;
	static const int monthDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (y < 1 || y > 9999 || mo < 1 || mo > 12 || d < 1 || d > monthDays[mo - 1] || h > 23 || mi > 59 || s > 59 || h < 0 || mi < 0 || s < 0)
		return std::nullopt;
	if (mo == 2 && d == 29 && !(y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return std::nullopt;
	return daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

void SettingsForm::declare(std::vector<Json> decl, std::string machineSetting)
{
	base_ = std::move(decl);
	machineSetting_ = std::move(machineSetting);
	narrow();
	/* which applied were places in the old declarations: none until evaluated */
	exposed_.clear();
	if (values_.kind != Json::Kind::Object) values_.kind = Json::Kind::Object;
}

void SettingsForm::declareMachines(std::vector<Json> machines)
{
	machines_ = std::move(machines);
	exposed_.clear();
	narrow();
}

void SettingsForm::narrow()
{
	decl_ = base_;
	if (machineSetting_.empty() || machines_.empty()) return;
	/* the machine the machine setting chooses (its "when", else its id, in
	 * any case), else the first */
	std::string value;
	if (const Json *v = values_.find(machineSetting_)) value = v->text;
	else
		for (const Json &d : base_)
			if (const Json *n = d.find("name"); n != nullptr && n->text == machineSetting_)
				if (const Json *def = d.find("default")) value = def->text;
	auto folded = [](std::string s) {
		for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	};
	const Json *machine = &machines_.front();
	for (const Json &m : machines_)
	{
		bool selects = false;
		if (const Json *when = m.find("when"); when != nullptr && !when->items.empty())
		{
			for (const Json &w : when->items) selects = selects || folded(w.text) == folded(value);
		}
		else if (const Json *id = m.find("id"))
			selects = folded(id->text) == folded(value);
		if (selects)
		{
			machine = &m;
			break;
		}
	}
	const Json *overrides = machine->find("settingOverrides");
	if (overrides == nullptr) return;
	for (Json &d : decl_)
	{
		const Json *n = d.find("name");
		const Json *over = n != nullptr ? overrides->find(n->text) : nullptr;
		if (over == nullptr) continue;
		if (const Json *o = over->find("options")) setMember(d, "options", *o);
		if (const Json *def = over->find("default")) setMember(d, "default", *def);
	}
}

void SettingsForm::setSlots(std::string slotsJson)
{
	slots_ = slotsJson.empty() ? std::string("{}") : std::move(slotsJson);
}

void SettingsForm::setValues(Json values)
{
	values_ = std::move(values);
	if (values_.kind != Json::Kind::Object)
	{
		values_ = Json();
		values_.kind = Json::Kind::Object;
	}
}

bool SettingsForm::set(const std::string &name, const Value &value, std::string &error)
{
	const Json *d = declared(name);
	if (d == nullptr)
	{
		error = "no setting " + name + " applies here";
		return false;
	}
	const std::string type = typeOf(*d);
	Json v;
	if (type == "bool")
	{
		bool b = false;
		if (!fromValue(value, b))
		{
			error = name + " is true or false";
			return false;
		}
		v = Json::boolean_(b);
	}
	else if (type == "int" || type == "uint" || type == "long" || type == "ulong" || type == "datetime")
	{
		/* a whole number in its type's range, or a date and time typed as Chimera shows it */
		std::optional<Whole> n;
		if (type == "datetime")
		{
			std::string text;
			if (fromValue(value, text))
			{
				if (const std::optional<int64_t> at = parseDate(text)) n = *at;
			}
			else
				n = wholeOf(value);
			if (!n)
			{
				error = name + " is a date and time, written as 2000-01-01 00:00:00 (UTC)";
				return false;
			}
		}
		else if (!(n = wholeOf(value)))
		{
			error = name + " is a whole number";
			return false;
		}
		Whole lo = 0, hi = 0;
		wholeRange(type, lo, hi);
		if (const Json *mn = d->find("min"))
			if (const std::optional<Whole> m = wholeOfText(mn->text)) lo = std::max(lo, *m);
		if (const Json *mx = d->find("max"))
			if (const std::optional<Whole> m = wholeOfText(mx->text)) hi = std::min(hi, *m);
		if (*n < lo || *n > hi)
		{
			error = name + " goes from " + (type == "datetime" ? dateText(static_cast<int64_t>(lo)) : wholeText(lo)) + " to " +
				(type == "datetime" ? dateText(static_cast<int64_t>(hi)) : wholeText(hi));
			return false;
		}
		v.kind = Json::Kind::Number;
		v.text = wholeText(*n);
	}
	else if (type == "float")
	{
		double f = 0;
		if (!fromValue(value, f))
		{
			error = name + " is a number";
			return false;
		}
		v = Json::number(f);
	}
	else
	{
		std::string s;
		if (!fromValue(value, s))
		{
			error = name + " is text";
			return false;
		}
		if (type == "enum")
		{
			bool known = false;
			if (const Json *o = d->find("options"))
				for (const Json &opt : o->items) known = known || opt.text == s;
			if (!known)
			{
				error = s + " is not one of " + name + "'s options";
				return false;
			}
		}
		v = Json::string(s);
	}
	setMember(values_, name, std::move(v));
	return true;
}

const Json *SettingsForm::declared(const std::string &name) const
{
	for (size_t i : exposed_)
		if (const Json *n = decl_[i].find("name"); n != nullptr && n->text == name) return &decl_[i];
	return nullptr;
}

void SettingsForm::evaluate()
{
	/* the machine may have changed, and with it what a setting offers */
	narrow();
	exposed_ = applicable(decl_, slots_, values_, machineSetting_);
	if (!listMachine_ && !machineSetting_.empty())
		exposed_.erase(std::remove_if(exposed_.begin(), exposed_.end(), [this](size_t i) {
			const Json *n = decl_[i].find("name");
			return n != nullptr && n->text == machineSetting_;
		}), exposed_.end());
	exposed_.erase(std::remove_if(exposed_.begin(), exposed_.end(), [this](size_t i) {
		const Json *n = decl_[i].find("name");
		return n != nullptr && std::find(unlisted_.begin(), unlisted_.end(), n->text) != unlisted_.end();
	}), exposed_.end());
}

const Json *SettingsForm::declaration(const std::string &name) const
{
	for (const Json &d : decl_)
		if (const Json *n = d.find("name"); n != nullptr && n->text == name) return &d;
	return nullptr;
}

bool SettingsForm::applies(const std::string &name) const
{
	for (size_t i : applicable(decl_, slots_, values_, machineSetting_))
		if (const Json *n = decl_[i].find("name"); n != nullptr && n->text == name) return true;
	return false;
}

Json SettingsForm::recorded() const
{
	Json out;
	out.kind = Json::Kind::Object;
	auto record = [&](const Json &d) {
		const Json *name = d.find("name");
		if (name == nullptr) return;
		const Json *v = values_.find(name->text);
		if (v == nullptr) v = d.find("default");
		if (v != nullptr) setMember(out, name->text, *v);
	};
	for (size_t i : exposed_) record(decl_[i]);
	/* the machine's chooser is a setting like any other: the slot gates and
	 * the engine's checks read it; and so is one offered on its own */
	if (!machineSetting_.empty())
		for (const Json &d : decl_)
			if (const Json *n = d.find("name"); n != nullptr && n->text == machineSetting_) record(d);
	for (const std::string &name : unlisted_)
		if (const Json *d = declaration(name); d != nullptr && out.find(name) == nullptr && applies(name)) record(*d);
	return out;
}

std::vector<SettingsForm::Changed> SettingsForm::changed() const
{
	/* as Chimera words them: true or false, (empty) for nothing, numbers as they are */
	auto shown = [](const Value &v) -> std::string {
		if (const bool *b = std::get_if<bool>(&v)) return *b ? "true" : "false";
		if (const int64_t *i = std::get_if<int64_t>(&v)) return std::to_string(*i);
		if (const double *d = std::get_if<double>(&v))
		{
			char text[32];
			std::snprintf(text, sizeof text, "%g", *d);
			return text;
		}
		if (const std::string *s = std::get_if<std::string>(&v)) return s->empty() ? "(empty)" : *s;
		return "";
	};
	std::vector<Changed> out;
	const Json none = [] {
		Json j;
		j.kind = Json::Kind::Object;
		return j;
	}();
	for (size_t i : exposed_)
	{
		const Json &d = decl_[i];
		const std::string value = shown(valueOf(d, values_)), standard = shown(valueOf(d, none));
		if (value == standard) continue;
		const Json *label = d.find("display");
		const Json *name = d.find("name");
		out.push_back({label != nullptr && !label->text.empty() ? label->text : name != nullptr ? name->text : "", value, standard});
	}
	return out;
}

Value SettingsForm::valueOf(const Json &decl, const Json &values)
{
	const Json *n = decl.find("name");
	const Json *v = n != nullptr ? values.find(n->text) : nullptr;
	if (v == nullptr) v = decl.find("default");
	if (v == nullptr) return {};
	switch (v->kind)
	{
	case Json::Kind::Bool: return v->boolean;
	case Json::Kind::Number:
	{
		const std::string type = typeOf(decl);
		if (type == "float") return v->number();
		/* a date as it is shown, and the 64-bit whole numbers as their digits (a
		 * window's numbers are doubles, and would lose the last ones) */
		if (const std::optional<Whole> n = wholeOfText(v->text))
		{
			if (type == "datetime" && *n >= kFirstSecond && *n <= kLastSecond) return dateText(static_cast<int64_t>(*n));
			if (type == "uint" || type == "long" || type == "ulong") return wholeText(*n);
			if (*n >= INT64_MIN && *n <= INT64_MAX) return static_cast<int64_t>(*n);
		}
		const double d = v->number();
		return std::floor(d) == d ? Value(static_cast<int64_t>(d)) : Value(d);
	}
	case Json::Kind::String: return v->text;
	default: return json::write(*v);
	}
}

std::vector<size_t> SettingsForm::applicable(const std::vector<Json> &decl, const std::string &slots,
	const Json &values, const std::string &machineSetting)
{
	/* which settings apply is the engine's to say: the game files and the
	 * other settings' values decide */
	Json array;
	array.kind = Json::Kind::Array;
	array.items = decl;
	const std::string declText = json::write(array), valueText = json::write(values);
	uint64_t len = 0;
	const char *out = ce_settings_evaluate(declText.data(), declText.size(), slots.data(), slots.size(),
		valueText.data(), valueText.size(), &len);
	/* and a setting that names machines belongs to those alone */
	std::string machine;
	if (!machineSetting.empty())
		for (const Json &d : decl)
			if (const Json *n = d.find("name"); n != nullptr && n->text == machineSetting)
			{
				const Json *v = values.find(machineSetting);
				if (v == nullptr) v = d.find("default");
				if (v != nullptr && v->kind == Json::Kind::String) machine = v->text;
			}
	std::vector<size_t> shown;
	Json list;
	std::string error;
	if (out != nullptr && json::parse(std::string_view(out, static_cast<size_t>(len)), list, error))
		for (const Json &e : list.items)
			if (const Json *i = e.find("index"); i != nullptr && i->number() >= 0 && i->number() < decl.size())
			{
				const Json &d = decl[static_cast<size_t>(i->number())];
				const Json *when = d.find("when");
				bool fits = when == nullptr || when->kind != Json::Kind::Array || machineSetting.empty();
				if (!fits)
					for (const Json &m : when->items) fits = fits || m.text == machine;
				if (fits) shown.push_back(static_cast<size_t>(i->number()));
			}
	return shown;
}

std::string SettingsForm::machineValue() const
{
	if (machineSetting_.empty()) return {};
	const Json *v = values_.find(machineSetting_);
	if (v == nullptr)
		for (const Json &d : decl_)
			if (const Json *n = d.find("name"); n != nullptr && n->text == machineSetting_) v = d.find("default");
	return v != nullptr ? v->text : std::string();
}

std::vector<const Json *> SettingsForm::offered() const
{
	std::vector<const Json *> out;
	const std::string machine = machineValue();
	for (const Json &p : presets_)
	{
		const Json *when = p.find("when");
		bool fits = when == nullptr || when->items.empty() || machine.empty();
		if (!fits)
			for (const Json &w : when->items)
			{
				std::string a = w.text, b = machine;
				for (char &c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				for (char &c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				fits = fits || a == b;
			}
		if (fits) out.push_back(&p);
	}
	return out;
}

bool SettingsForm::applyPreset(const std::string &id, std::string &label, std::vector<std::string> &skipped, std::string &error,
	int64_t *applied)
{
	const Json *preset = nullptr;
	for (const Json *p : offered())
		if (const Json *i = p->find("id"); i != nullptr && i->text == id) preset = p;
	if (preset == nullptr)
	{
		error = "no preset " + id + " for this machine";
		return false;
	}
	const Json *l = preset->find("label");
	label = l != nullptr && !l->text.empty() ? l->text : id;
	evaluate();
	if (const Json *values = preset->find("values"))
		for (const auto &[name, value] : values->members)
		{
			if (name == machineSetting_ || name == "renderer") continue;
			Value v;
			switch (value.kind)
			{
			case Json::Kind::Bool: v = value.boolean; break;
			case Json::Kind::Number:
				v = std::floor(value.number()) == value.number() ? Value(static_cast<int64_t>(value.number())) : Value(value.number());
				break;
			default: v = value.text; break;
			}
			std::string why;
			if (!set(name, v, why)) skipped.push_back(name);
			else if (applied != nullptr) (*applied)++;
		}
	evaluate();
	return true;
}

std::string SettingsForm::PresetRows::columnName(int32_t column) const
{
	static const char *const names[] = {"id", "label", "description"};
	return column >= 0 && column < 3 ? names[column] : "";
}

Value SettingsForm::PresetRows::cell(int64_t row, int32_t column) const
{
	const auto list = form_.offered();
	if (row < 0 || row >= static_cast<int64_t>(list.size())) return {};
	const Json &p = *list[static_cast<size_t>(row)];
	auto text = [&](const char *k) { const Json *j = p.find(k); return j != nullptr ? j->text : std::string(); };
	switch (column)
	{
	case 0: return text("id");
	case 1: return text("label").empty() ? text("id") : text("label");
	case 2: return text("description");
	default: return {};
	}
}

std::string SettingsForm::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "label", "type", "value", "options", "optionLabels", "min", "max", "description", "edited"};
	return column >= 0 && column < kColumns ? names[column] : "";
}

Value SettingsForm::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= static_cast<int64_t>(form_.exposed_.size())) return {};
	const Json &d = form_.decl_[form_.exposed_[static_cast<size_t>(row)]];
	auto text = [&](const char *key) -> Value {
		const Json *j = d.find(key);
		return j == nullptr ? Value(std::string()) : Value(j->kind == Json::Kind::String ? j->text : json::write(*j));
	};
	switch (column)
	{
	case kName: return text("name");
	case kLabel: return text("display");
	case kType: return typeOf(d);
	case kValue: return valueOf(d, form_.values_);
	case kOptions: return joined(d.find("options"), nullptr);
	case kOptionLabels: return joined(d.find("options"), d.find("optionLabels"));
	case kMin: return d.find("min") != nullptr ? Value(d.find("min")->number()) : Value();
	case kMax: return d.find("max") != nullptr ? Value(d.find("max")->number()) : Value();
	case kDescription: return text("description");
	case kEdited: return form_.applied_ != nullptr && valueOf(d, form_.values_) != valueOf(d, *form_.applied_);
	default: return {};
	}
}

} // namespace chimera::vm
