#include "chimera/vm/settings_vm.hpp"

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

SettingsViewModel::SettingsViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("settings", dispatcher), session_(session)
{
	applied_.kind = values_.kind = Json::Kind::Object;
	expose("pending", "edits not applied yet", pending);
	expose("shown", "settings that apply to this machine and game", shown);
	expose("status", "what the settings last had to say", status);
	rows("settings", "one row a setting: name, label, type, value, options, option labels, min, max, description, edited", rows_);

	command("set", "changes a setting (applied by apply)", {"name", "value"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			const Json *d = declared(name);
			if (d == nullptr)
			{
				error = "no setting " + name + " applies here";
				return {};
			}
			if (a.size() < 2)
			{
				error = "argument 2 is missing";
				return {};
			}
			const std::string type = d->find("type") != nullptr ? d->find("type")->text : "";
			Json v;
			if (type == "bool")
			{
				bool b = false;
				if (!arg(a, 1, b, error)) return {};
				v = Json::boolean_(b);
			}
			else if (type == "int")
			{
				int64_t i = 0;
				if (!arg(a, 1, i, error)) return {};
				const Json *mn = d->find("min"), *mx = d->find("max");
				if ((mn != nullptr && i < mn->number()) || (mx != nullptr && i > mx->number()))
				{
					error = name + " goes from " + (mn ? mn->text : "?") + " to " + (mx ? mx->text : "?");
					return {};
				}
				v = Json::integer(i);
			}
			else if (type == "float")
			{
				double f = 0;
				if (!arg(a, 1, f, error)) return {};
				v = Json::number(f);
			}
			else
			{
				std::string s;
				if (!arg(a, 1, s, error)) return {};
				if (type == "enum")
				{
					bool known = false;
					if (const Json *o = d->find("options"))
						for (const Json &opt : o->items) known = known || opt.text == s;
					if (!known)
					{
						error = s + " is not one of " + name + "'s options";
						return {};
					}
				}
				v = Json::string(s);
			}
			setMember(values_, name, std::move(v));
			pending.set(json::write(values_) != json::write(applied_));
			evaluate();
			return {};
		}, &session_.isOpen);
	command("discard", "forgets the edits not applied", {}, [this](const Args &, std::string &) -> Value {
		values_ = applied_;
		pending.set(false);
		evaluate();
		return {};
	}, &pending);
	command("apply", "gives the edits to the machine: live when the core can take them, else it starts again with them",
		{}, [this](const Args &, std::string &) -> Value {
			const Json values = values_;
			status.set("Applying...");
			session_.changeSettings(json::write(values), [this, values](bool ok, std::string what) {
				if (ok)
				{
					applied_ = values;
					pending.set(json::write(values_) != json::write(applied_));
				}
				status.set(what);
				evaluate();
			});
			return {};
		}, &pending);

	/* each machine opened: its declaration, and the values it was opened with */
	opensWatch_ = session_.opens.changed.connect([this] {
		session_.onMachine([this](ce_session *s) {
			if (s == nullptr) return;
			std::string config;
			const char *perr = nullptr;
			if (ce_package *p = ce_package_open(session_.packageOnMachine().c_str(), &perr))
			{
				uint64_t len = 0;
				if (const uint8_t *d = ce_package_entry(p, "waterbox.config", &len))
					config.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
				ce_package_free(p);
			}
			const char *game = ce_session_game_settings(s);
			const std::string gameText = game != nullptr ? game : "";
			const std::string values = session_.settingsOnMachine(), slots = session_.slotsOnMachine();
			later([this, config, gameText, values, slots] {
				decl_.clear();
				std::string error;
				Json cfg, extra, v;
				if (json::parse(config, cfg, error))
					if (const Json *list = cfg.find("settings")) decl_ = list->items;
				if (!gameText.empty() && json::parse(gameText, extra, error))
					for (const Json &d : extra.items) decl_.push_back(d);
				Json array;
				array.kind = Json::Kind::Array;
				array.items = decl_;
				declText_ = json::write(array);
				slots_ = slots;
				if (!json::parse(values, v, error) || v.kind != Json::Kind::Object) v = Json(), v.kind = Json::Kind::Object;
				applied_ = values_ = v;
				pending.set(false);
				status.set(std::to_string(decl_.size()) + " settings declared");
				evaluate();
			});
		});
	});
}

void SettingsViewModel::evaluate()
{
	/* which settings apply is the engine's to say: the game files and the
	 * other settings' values decide */
	exposed_.clear();
	uint64_t len = 0;
	const std::string values = json::write(values_);
	const char *out = ce_settings_evaluate(declText_.data(), declText_.size(), slots_.data(), slots_.size(),
		values.data(), values.size(), &len);
	Json list;
	std::string error;
	if (out != nullptr && json::parse(std::string_view(out, static_cast<size_t>(len)), list, error))
		for (const Json &e : list.items)
			if (const Json *i = e.find("index"); i != nullptr && i->number() >= 0 && i->number() < decl_.size())
				exposed_.push_back(static_cast<size_t>(i->number()));
	shown.set(static_cast<int64_t>(exposed_.size()));
	rows_.reset.emit();
}

const Json *SettingsViewModel::declared(const std::string &name) const
{
	for (size_t i : exposed_)
		if (const Json *n = decl_[i].find("name"); n != nullptr && n->text == name) return &decl_[i];
	return nullptr;
}

Value SettingsViewModel::valueOf(const Json &decl, const Json &values) const
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

std::string SettingsViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "label", "type", "value", "options", "optionLabels", "min", "max", "description", "edited"};
	return column >= 0 && column < kColumns ? names[column] : "";
}

Value SettingsViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= static_cast<int64_t>(vm_.exposed_.size())) return {};
	const Json &d = vm_.decl_[vm_.exposed_[static_cast<size_t>(row)]];
	auto text = [&](const char *key) -> Value {
		const Json *j = d.find(key);
		return j == nullptr ? Value(std::string()) : Value(j->kind == Json::Kind::String ? j->text : json::write(*j));
	};
	switch (column)
	{
	case kName: return text("name");
	case kLabel: return text("display");
	case kType: return text("type");
	case kValue: return vm_.valueOf(d, vm_.values_);
	case kOptions: return joined(d.find("options"), nullptr);
	case kOptionLabels: return joined(d.find("options"), d.find("optionLabels"));
	case kMin: return d.find("min") != nullptr ? Value(d.find("min")->number()) : Value();
	case kMax: return d.find("max") != nullptr ? Value(d.find("max")->number()) : Value();
	case kDescription: return text("description");
	case kEdited:
	{
		const Value now = vm_.valueOf(d, vm_.values_), was = vm_.valueOf(d, vm_.applied_);
		return now != was;
	}
	default: return {};
	}
}

} // namespace chimera::vm
