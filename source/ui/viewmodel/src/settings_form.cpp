#include "chimera/vm/settings_form.hpp"

#include <algorithm>
#include <cmath>

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
} // namespace

void SettingsForm::declare(std::vector<Json> decl, std::string machineSetting)
{
	decl_ = std::move(decl);
	machineSetting_ = std::move(machineSetting);
	if (values_.kind != Json::Kind::Object) values_.kind = Json::Kind::Object;
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
	const std::string type = d->find("type") != nullptr ? d->find("type")->text : "";
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
	else if (type == "int")
	{
		int64_t i = 0;
		if (!fromValue(value, i))
		{
			error = name + " is a whole number";
			return false;
		}
		const Json *mn = d->find("min"), *mx = d->find("max");
		if ((mn != nullptr && i < mn->number()) || (mx != nullptr && i > mx->number()))
		{
			error = name + " goes from " + (mn ? mn->text : "?") + " to " + (mx ? mx->text : "?");
			return false;
		}
		v = Json::integer(i);
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
	exposed_ = applicable(decl_, slots_, values_, machineSetting_);
	if (!listMachine_ && !machineSetting_.empty())
		exposed_.erase(std::remove_if(exposed_.begin(), exposed_.end(), [this](size_t i) {
			const Json *n = decl_[i].find("name");
			return n != nullptr && n->text == machineSetting_;
		}), exposed_.end());
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
	 * the engine's checks read it */
	if (!machineSetting_.empty())
		for (const Json &d : decl_)
			if (const Json *n = d.find("name"); n != nullptr && n->text == machineSetting_) record(d);
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
		const double d = v->number();
		const Json *t = decl.find("type");
		if (t != nullptr && t->text == "float") return d;
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
	case kType: return text("type");
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
