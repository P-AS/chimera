#include "chimera/vm/controls_vm.hpp"

#include <map>

#include <algorithm>
#include <cmath>
#include <regex>

namespace chimera::vm {

using json::Json;

namespace {
Json parsedObject(const std::string &text)
{
	Json j;
	std::string error;
	if (!json::parse(text, j, error) || j.kind != Json::Kind::Object)
	{
		j = Json();
		j.kind = Json::Kind::Object;
	}
	return j;
}

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

void removeMember(Json &obj, const std::string &name)
{
	for (auto it = obj.members.begin(); it != obj.members.end(); ++it)
		if (it->first == name)
		{
			obj.members.erase(it);
			return;
		}
}

std::string trimmed(std::string t)
{
	while (!t.empty() && t.front() == ' ') t.erase(0, 1);
	while (!t.empty() && t.back() == ' ') t.pop_back();
	return t;
}

/* a box's keys, as Chimera's InputWidget keeps them: the text split at its
 * commas, the blank ones left out */
std::vector<std::string> pieces(const std::string &keys)
{
	std::vector<std::string> out;
	size_t at = 0;
	while (at <= keys.size())
	{
		const size_t comma = keys.find(',', at);
		std::string one = keys.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
		if (!trimmed(one).empty()) out.push_back(one);
		if (comma == std::string::npos) break;
		at = comma + 1;
	}
	return out;
}

std::string joined(const std::vector<std::string> &keys)
{
	std::string out;
	for (const std::string &k : keys) out += (out.empty() ? "" : ",") + k;
	return out;
}

bool hasKey(const std::vector<std::string> &keys, const std::string &key)
{
	return std::any_of(keys.begin(), keys.end(), [&](const std::string &k) { return trimmed(k) == key; });
}

/* the inputs a box takes only from its menu of special bindings, as Chimera's */
bool special(const std::string &key)
{
	return key == "Escape" || key == "WMouse L" || key == "WMouse M" || key == "WMouse R" || key == "WMouse 1" || key == "WMouse 2";
}

bool sameBind(const AnalogBind &a, const AnalogBind &b)
{
	return a.value == b.value && std::abs(a.mult - b.mult) < 1e-9 && std::abs(a.deadzone - b.deadzone) < 1e-9 &&
		a.positive == b.positive && a.negative == b.negative;
}

/* the tabs, in the order their first control comes */
std::string groupsOf(const std::vector<std::string> &names)
{
	Json list;
	list.kind = Json::Kind::Array;
	std::vector<std::string> seen;
	for (const std::string &n : names)
	{
		const std::string g = ControlsViewModel::groupOf(n);
		if (std::find(seen.begin(), seen.end(), g) != seen.end()) continue;
		seen.push_back(g);
		list.items.push_back(Json::string(g));
	}
	return json::write(list);
}
} // namespace

std::string ControlsViewModel::groupOf(const std::string &name)
{
	static const std::regex player("^P(\\d+)\\s");
	std::smatch m;
	return std::regex_search(name, m, player) ? "Player " + m[1].str() : "Console";
}

ControlsViewModel::ControlsViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("controls", dispatcher), session_(session)
{
	expose("saved", "the buttons' keys that differ from the package, every core's, as {\"core\": {\"button\": \"keys\"}}", saved, true);
	expose("savedAutofire", "the autofire keys that differ from the package, as saved is", savedAutofire, true);
	expose("savedAnalog", "the analog controls' bindings that differ from the package, as {\"core\": {\"control\": "
		"{\"Value\", \"Mult\", \"Deadzone\", \"ButtonBindPositive\", \"ButtonBindNegative\"}}}", savedAnalog, true);
	expose("fromChimera", "Chimera's bindings carried over, by controller name ({\"AllTrollers\", \"AllTrollersAutoFire\", "
		"\"AllTrollersAnalog\"}), and \"taken\", the cores that took theirs the first time they opened", fromChimera, true);
	expose("autoTab", "Auto Tab: a key pressed replaces a box's keys and the next box takes the next; off, it is one more", autoTab, true);
	expose("core", "the core whose controls are listed", core);
	expose("title", "the window's title: the controller's name and \"Configuration\"", title);
	expose("buttonGroups", "the tabs of the buttons, in order (a JSON array: \"Player 1\"..., \"Console\")", buttonGroups);
	expose("axisGroups", "the tabs of the analog controls, in order", axisGroups);
	expose("editing", "the window's copy is open: changes wait for save", editing);
	expose("pictureShown", "the picture of the controller the package ships, counted up each time another is shown (0: none)", pictureShown);
	expose("status", "what Controls last had to say", status);
	rows("buttons", "one row a button: name, shown, group, keys (the copy's while editing), the package's keys, changed", normalRows_);
	rows("autofire", "one row a button, its autofire keys: as the buttons", autofireRows_);
	rows("analog", "one row an analog control: name, group, value (the axis), mult, deadzone, positive, negative "
		"(the keys pushing it to either end), changed", analogRows_);

	command("edit", "opens the window's copy of the controls, from what the machine has now", {}, [this](const Args &, std::string &) -> Value {
		begin();
		return {};
	});
	command("press", "a key or gamepad input pressed in a box, as Chimera's: Escape empties it; with Auto Tab the key "
		"replaces its keys, without it is one more. True when the next box takes the next key. Table: normal, "
		"autofire, positive or negative (an analog control's keys)", {"table", "index", "key"},
		[this](const Args &a, std::string &error) -> Value {
			Table t = Normal;
			size_t i = 0;
			std::string key;
			if (!table(a, t, error) || !index(a, 1, t, i, error) || !arg(a, 2, key, error)) return {};
			begin();
			std::string &keys = keysOf(t, i);
			if (key == "Escape")
			{
				keys.clear();
				changed();
				return autoTab.get();
			}
			/* never bound, and the inputs only the menu gives */
			if (key.empty() || key == "Alt+F4" || special(key)) return false;
			std::vector<std::string> now = pieces(keys);
			if (!hasKey(now, key))
			{
				if (autoTab.get()) now.clear();
				now.push_back(key);
			}
			keys = joined(now);
			changed();
			return autoTab.get();
		});
	command("special", "one of the bindings a key cannot give, added to a box: Escape, WMouse L, WMouse M, WMouse R, WMouse 1, "
		"WMouse 2. True when the next box takes the next key", {"table", "index", "name"},
		[this](const Args &a, std::string &error) -> Value {
			Table t = Normal;
			size_t i = 0;
			std::string name;
			if (!table(a, t, error) || !index(a, 1, t, i, error) || !arg(a, 2, name, error)) return {};
			if (!special(name))
			{
				error = name + " is not a special binding";
				return {};
			}
			begin();
			std::string &keys = keysOf(t, i);
			std::vector<std::string> now = pieces(keys);
			if (!hasKey(now, name)) now.push_back(name);
			keys = joined(now);
			changed();
			return autoTab.get();
		});
	command("bind", "a box's keys, replaced (\"Z,X1 A\"; \"\" for none)", {"table", "index", "keys"},
		[this](const Args &a, std::string &error) -> Value {
			Table t = Normal;
			size_t i = 0;
			std::string keys;
			if (!table(a, t, error) || !index(a, 1, t, i, error) || !arg(a, 2, keys, error)) return {};
			begin();
			keysOf(t, i) = keys;
			changed();
			return {};
		});
	command("setAnalog", "an analog control's axis (\"value\": \"X1 LeftThumbX Axis\", \"WMouse X\"), sensitivity (\"mult\", "
		"-2 to 2) or dead zone (\"deadzone\", 0 to 0.5)", {"index", "field", "value"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			std::string field;
			if (!index(a, 0, Positive, i, error) || !arg(a, 1, field, error)) return {};
			begin();
			AnalogBind &b = analogDraft_[i];
			if (field == "value")
			{
				if (!arg(a, 2, b.value, error)) return {};
			}
			else if (field == "mult" || field == "deadzone")
			{
				double v = 0;
				if (!arg(a, 2, v, error)) return {};
				(field == "mult" ? b.mult : b.deadzone) = field == "mult" ? std::clamp(v, -2.0, 2.0) : std::clamp(v, 0.0, 0.5);
			}
			else
			{
				error = "no field " + field + ": value, mult or deadzone";
				return {};
			}
			changed();
			return {};
		});
	command("caught", "the axis an analog control's Bind! caught, as Chimera takes it: the pointer's axis caught again "
		"where it is already bound goes from where the pointer is (WMouse) to how far it moves (RMouse), and back",
		{"index", "axis"}, [this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			std::string axis;
			if (!index(a, 0, Positive, i, error) || !arg(a, 1, axis, error)) return {};
			begin();
			AnalogBind &b = analogDraft_[i];
			for (const char *xy : {"X", "Y"})
			{
				const std::string absolute = std::string("WMouse ") + xy, relative = std::string("RMouse ") + xy;
				if (axis != absolute) continue;
				if (b.value == absolute) axis = relative;
				else if (b.value == relative) axis = absolute;
			}
			b.value = axis;
			changed();
			return axis;
		});
	command("flip", "an analog control's sensitivity turned the other way (Flip Axis)", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			if (!index(a, 0, Positive, i, error)) return {};
			begin();
			analogDraft_[i].mult = -analogDraft_[i].mult;
			changed();
			return {};
		});
	command("unbind", "an analog control's axis taken off (Unbind!)", {"index"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		if (!index(a, 0, Positive, i, error)) return {};
		begin();
		analogDraft_[i].value.clear();
		changed();
		return {};
	});
	command("loadDefaults", "the copy: the package's controls (kept only by save)", {}, [this](const Args &, std::string &) -> Value {
		begin();
		draft_[Normal] = session_.defaultBindingTexts();
		draft_[Autofire] = session_.defaultAutofireTexts();
		analogDraft_ = session_.defaultAnalogBinds();
		draft_[Normal].resize(session_.buttonNames().size());
		draft_[Autofire].resize(session_.buttonNames().size());
		analogDraft_.resize(session_.axes().size());
		changed();
		return {};
	});
	command("clear", "the copy: every box empty and every analog control unbound (kept only by save)", {},
		[this](const Args &, std::string &) -> Value {
			begin();
			for (auto &t : draft_)
				for (std::string &k : t) k.clear();
			for (AnalogBind &b : analogDraft_)
			{
				b.value.clear();
				b.positive.clear();
				b.negative.clear();
			}
			changed();
			return {};
		});
	command("save", "the copy given to the machine, and what differs from the package remembered for this core", {},
		[this](const Args &, std::string &) -> Value {
			if (editing.get()) keep();
			return {};
		});
	command("replaceKeys", "Chimera's rewrite of the keys bound when Merge L+R modifier keys changes: the key names the "
		"object names replaced ({\"LeftShift\": \"Shift\"}) in every core's controls remembered and the machine's now "
		"(kept), or in the copy when it is open (kept by Save)", {"names"}, [this](const Args &a, std::string &error) -> Value {
			std::string text;
			std::map<std::string, std::string> names;
			if (!arg(a, 0, text, error) || !keyNamesFrom(text, names, error)) return {};
			/* every core's remembered keys */
			auto rewrite = [&](Property<std::string> &prop, bool analog) {
				Json all = parsedObject(prop.get());
				for (auto &[name, mine] : all.members)
					for (auto &[control, keys] : mine.members)
					{
						if (!analog && keys.kind == Json::Kind::String) keys.text = replaceKeyNames(keys.text, names);
						if (analog)
							for (auto &[field, value] : keys.members)
								if ((field == "ButtonBindPositive" || field == "ButtonBindNegative") && value.kind == Json::Kind::String)
									value.text = replaceKeyNames(value.text, names);
					}
				prop.set(json::write(all));
			};
			rewrite(saved, false);
			rewrite(savedAutofire, false);
			rewrite(savedAnalog, true);
			/* and the machine's now, the package's keys among them, as Chimera's
			 * settings hold every system's */
			if (core.get().empty()) return {};
			const bool open = editing.get();
			begin();
			for (auto &table : draft_)
				for (std::string &keys : table) keys = replaceKeyNames(keys, names);
			for (AnalogBind &bind : analogDraft_)
			{
				bind.positive = replaceKeyNames(bind.positive, names);
				bind.negative = replaceKeyNames(bind.negative, names);
			}
			if (!open) keep();
			else changed();
			return {};
		});
	command("hasBinding", "whether a key holds one of the machine's buttons now (Chimera's HasBinding: a right click "
		"bound to a button does not open the window's menu, a double click with the left one does not go full screen)",
		{"key"}, [this](const Args &a, std::string &error) -> Value {
			std::string key;
			if (!arg(a, 0, key, error)) return {};
			for (const std::string &keys : session_.bindingTexts())
				if (hasKey(pieces(keys), key)) return true;
			return false;
		});
	command("cancel", "the copy let go: the machine keeps its controls", {}, [this](const Args &, std::string &) -> Value {
		editing.set(false);
		changed();
		return {};
	});

	opensWatch_ = session_.opens.changed.connect([this] {
		core.set(session_.coreName.get());
		title.set(session_.controllerName() + " Configuration");
		buttonGroups.set(groupsOf(session_.buttonNames()));
		std::vector<std::string> axisNames;
		for (const SessionViewModel::Axis &x : session_.axes()) axisNames.push_back(x.name);
		axisGroups.set(groupsOf(axisNames));
		pictureShown.set(session_.controllerPicture().empty() ? 0 : pictureShown.get() + 1);
		takeFromChimera();
		apply();
		/* a copy open goes on with this machine's controls */
		if (editing.get())
		{
			editing.set(false);
			begin();
		}
	});
	/* the settings file restoring what was saved */
	auto restored = [this] {
		if (!core.get().empty()) apply();
	};
	savedWatch_ = saved.changed.connect(restored);
	savedAutofireWatch_ = savedAutofire.changed.connect(restored);
	savedAnalogWatch_ = savedAnalog.changed.connect(restored);
}

bool ControlsViewModel::table(const Args &a, Table &out, std::string &error) const
{
	std::string t;
	if (!arg(a, 0, t, error)) return false;
	if (t == "normal") out = Normal;
	else if (t == "autofire") out = Autofire;
	else if (t == "positive") out = Positive;
	else if (t == "negative") out = Negative;
	else
	{
		error = "no table " + t + ": normal, autofire, positive or negative";
		return false;
	}
	return true;
}

bool ControlsViewModel::index(const Args &a, size_t at, Table t, size_t &out, std::string &error) const
{
	int64_t i = 0;
	if (!arg(a, at, i, error)) return false;
	const bool analog = t == Positive || t == Negative;
	const size_t count = analog ? session_.axes().size() : session_.buttonNames().size();
	if (i < 0 || static_cast<size_t>(i) >= count)
	{
		error = std::string(analog ? "no analog control " : "no button ") + std::to_string(i);
		return false;
	}
	out = static_cast<size_t>(i);
	return true;
}

std::string &ControlsViewModel::keysOf(Table t, size_t i)
{
	if (t == Positive) return analogDraft_[i].positive;
	if (t == Negative) return analogDraft_[i].negative;
	return draft_[t][i];
}

void ControlsViewModel::begin()
{
	if (editing.get()) return;
	draft_[Normal] = session_.bindingTexts();
	draft_[Autofire] = session_.autofireTexts();
	analogDraft_ = session_.analogBinds();
	draft_[Normal].resize(session_.buttonNames().size());
	draft_[Autofire].resize(session_.buttonNames().size());
	analogDraft_.resize(session_.axes().size());
	editing.set(true);
	changed();
}

void ControlsViewModel::keep()
{
	const std::vector<std::string> &names = session_.buttonNames();
	auto remember = [&](Property<std::string> &prop, const std::vector<std::string> &now, const std::vector<std::string> &theirs) {
		Json all = parsedObject(prop.get());
		Json mine = all.find(core.get()) != nullptr ? *all.find(core.get()) : Json();
		if (mine.kind != Json::Kind::Object) mine = parsedObject("{}");
		for (size_t b = 0; b < names.size(); b++)
		{
			const std::string &k = b < now.size() ? now[b] : std::string(), &p = b < theirs.size() ? theirs[b] : std::string();
			if (k == p) removeMember(mine, names[b]);
			else setMember(mine, names[b], Json::string(k));
		}
		if (mine.members.empty()) removeMember(all, core.get());
		else setMember(all, core.get(), mine);
		prop.set(json::write(all));
	};
	remember(saved, draft_[Normal], session_.defaultBindingTexts());
	remember(savedAutofire, draft_[Autofire], session_.defaultAutofireTexts());
	{
		Json all = parsedObject(savedAnalog.get());
		Json mine = all.find(core.get()) != nullptr ? *all.find(core.get()) : Json();
		if (mine.kind != Json::Kind::Object) mine = parsedObject("{}");
		const std::vector<SessionViewModel::Axis> &axes = session_.axes();
		const std::vector<AnalogBind> &theirs = session_.defaultAnalogBinds();
		for (size_t i = 0; i < axes.size() && i < analogDraft_.size(); i++)
		{
			if (i < theirs.size() && sameBind(analogDraft_[i], theirs[i]))
			{
				removeMember(mine, axes[i].name);
				continue;
			}
			Json bind;
			std::string why;
			json::parse(analogBindJson(analogDraft_[i]), bind, why);
			setMember(mine, axes[i].name, bind);
		}
		if (mine.members.empty()) removeMember(all, core.get());
		else setMember(all, core.get(), mine);
		savedAnalog.set(json::write(all));
	}
	editing.set(false);
	apply();
	status.set("Saved the controls of " + core.get());
}

void ControlsViewModel::takeFromChimera()
{
	Json from = parsedObject(fromChimera.get());
	const std::string controller = session_.controllerName();
	if (from.members.empty() || core.get().empty() || controller.empty()) return;
	Json taken = from.find("taken") != nullptr ? *from.find("taken") : Json();
	if (taken.kind != Json::Kind::Array) taken.kind = Json::Kind::Array;
	for (const Json &t : taken.items)
		if (t.text == core.get()) return;
	bool any = false;
	/* the buttons' and the autofire keys: what differs from the package,
	 * unless the core has keys of its own already */
	const std::vector<std::string> &names = session_.buttonNames();
	auto take = [&](const char *section, Property<std::string> &prop, const std::vector<std::string> &theirs) {
		const Json *s = from.find(section);
		const Json *keys = s != nullptr ? s->find(controller) : nullptr;
		if (keys == nullptr || keys->kind != Json::Kind::Object) return;
		any = true;
		Json all = parsedObject(prop.get());
		if (all.find(core.get()) != nullptr) return;
		Json mine = parsedObject("{}");
		for (size_t b = 0; b < names.size(); b++)
			if (const Json *k = keys->find(names[b]); k != nullptr && k->kind == Json::Kind::String && (b >= theirs.size() || k->text != theirs[b]))
				setMember(mine, names[b], Json::string(k->text));
		if (mine.members.empty()) return;
		setMember(all, core.get(), mine);
		prop.set(json::write(all));
	};
	take("AllTrollers", saved, session_.defaultBindingTexts());
	take("AllTrollersAutoFire", savedAutofire, session_.defaultAutofireTexts());
	{
		const Json *s = from.find("AllTrollersAnalog");
		const Json *binds = s != nullptr ? s->find(controller) : nullptr;
		if (binds != nullptr && binds->kind == Json::Kind::Object)
		{
			any = true;
			Json all = parsedObject(savedAnalog.get());
			if (all.find(core.get()) == nullptr)
			{
				Json mine = parsedObject("{}");
				const std::vector<SessionViewModel::Axis> &axes = session_.axes();
				const std::vector<AnalogBind> &theirs = session_.defaultAnalogBinds();
				for (size_t i = 0; i < axes.size(); i++)
				{
					const Json *b = binds->find(axes[i].name);
					AnalogBind bind;
					if (b == nullptr || b->kind != Json::Kind::Object || !analogBindFrom(json::write(*b), bind)) continue;
					if (i < theirs.size() && sameBind(bind, theirs[i])) continue;
					Json written;
					std::string why;
					json::parse(analogBindJson(bind), written, why);
					setMember(mine, axes[i].name, written);
				}
				if (!mine.members.empty())
				{
					setMember(all, core.get(), mine);
					savedAnalog.set(json::write(all));
				}
			}
		}
	}
	if (!any) return;
	taken.items.push_back(Json::string(core.get()));
	setMember(from, "taken", taken);
	fromChimera.set(json::write(from));
}

void ControlsViewModel::apply()
{
	const std::vector<std::string> &names = session_.buttonNames();
	auto overlay = [&](const Property<std::string> &prop, std::vector<std::string> texts) {
		texts.resize(names.size());
		const Json all = parsedObject(prop.get());
		if (const Json *mine = all.find(core.get()))
			for (size_t b = 0; b < names.size(); b++)
				if (const Json *keys = mine->find(names[b]); keys != nullptr && keys->kind == Json::Kind::String) texts[b] = keys->text;
		return texts;
	};
	session_.setBindingTexts(overlay(saved, session_.defaultBindingTexts()));
	session_.setAutofireTexts(overlay(savedAutofire, session_.defaultAutofireTexts()));
	std::vector<AnalogBind> binds = session_.defaultAnalogBinds();
	const std::vector<SessionViewModel::Axis> &axes = session_.axes();
	binds.resize(axes.size());
	const Json all = parsedObject(savedAnalog.get());
	if (const Json *mine = all.find(core.get()))
		for (size_t i = 0; i < axes.size(); i++)
			if (const Json *b = mine->find(axes[i].name); b != nullptr && b->kind == Json::Kind::Object) analogBindFrom(json::write(*b), binds[i]);
	session_.setAnalogBinds(binds);
	changed();
}

void ControlsViewModel::changed()
{
	normalRows_.reset.emit();
	autofireRows_.reset.emit();
	analogRows_.reset.emit();
}

std::string ControlsViewModel::KeyRows::columnName(int32_t column) const
{
	static const char *const names[] = {"button", "shown", "group", "keys", "packageKeys", "changed"};
	return column >= 0 && column < 6 ? names[column] : "";
}

Value ControlsViewModel::KeyRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const size_t b = static_cast<size_t>(row);
	const bool autofire = table_ == Autofire;
	const std::vector<std::string> &now = vm_.editing.get() ? vm_.draft_[table_]
		: autofire ? vm_.session_.autofireTexts() : vm_.session_.bindingTexts();
	const std::vector<std::string> &theirs = autofire ? vm_.session_.defaultAutofireTexts() : vm_.session_.defaultBindingTexts();
	const std::string keys = b < now.size() ? now[b] : std::string(), package = b < theirs.size() ? theirs[b] : std::string();
	const std::string &name = vm_.session_.buttonNames()[b];
	switch (column)
	{
	case 0: return name;
	/* as Chimera's panel writes a control: underscores as spaces */
	case 1:
	{
		std::string shown = name;
		std::replace(shown.begin(), shown.end(), '_', ' ');
		return trimmed(shown);
	}
	case 2: return groupOf(name);
	case 3: return keys;
	case 4: return package;
	case 5: return keys != package;
	default: return {};
	}
}

std::string ControlsViewModel::AnalogRows::columnName(int32_t column) const
{
	static const char *const names[] = {"axis", "group", "value", "mult", "deadzone", "positive", "negative", "changed", "header"};
	return column >= 0 && column < 9 ? names[column] : "";
}

Value ControlsViewModel::AnalogRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const size_t i = static_cast<size_t>(row);
	const std::vector<AnalogBind> &now = vm_.editing.get() ? vm_.analogDraft_ : vm_.session_.analogBinds();
	const std::vector<AnalogBind> &theirs = vm_.session_.defaultAnalogBinds();
	const AnalogBind b = i < now.size() ? now[i] : AnalogBind(), p = i < theirs.size() ? theirs[i] : AnalogBind();
	const SessionViewModel::Axis &axis = vm_.session_.axes()[i];
	switch (column)
	{
	case 0: return axis.name;
	case 1: return groupOf(axis.name);
	case 2: return b.value;
	case 3: return b.mult;
	case 4: return b.deadzone;
	case 5: return b.positive;
	case 6: return b.negative;
	case 7: return !sameBind(b, p);
	case 8: return axis.header;
	default: return {};
	}
}

} // namespace chimera::vm
