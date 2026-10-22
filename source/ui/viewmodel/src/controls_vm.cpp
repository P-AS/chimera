#include "chimera/vm/controls_vm.hpp"

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

/* "A, B" with `key` added, unless it is there */
std::string withKey(const std::string &keys, const std::string &key)
{
	std::string rest = keys;
	size_t at = 0;
	while (at <= rest.size())
	{
		const size_t comma = rest.find(',', at);
		std::string one = rest.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
		while (!one.empty() && one.front() == ' ') one.erase(0, 1);
		while (!one.empty() && one.back() == ' ') one.pop_back();
		if (one == key) return keys;
		if (comma == std::string::npos) break;
		at = comma + 1;
	}
	return keys.empty() ? key : keys + ", " + key;
}
} // namespace

ControlsViewModel::ControlsViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("controls", dispatcher), session_(session)
{
	expose("saved", "the keys changed, every core's, as {\"core\": {\"button\": \"keys\"}}", saved, true);
	expose("core", "the core whose buttons are listed", core);
	expose("status", "what Controls last had to say", status);
	rows("buttons", "one row a button: name, its keys, the package's keys for it, changed", rows_);

	command("bind", "a button's keys, replaced (\"Z, X1 A\"; \"\" for none)", {"button", "keys"},
		[this](const Args &a, std::string &error) -> Value {
			size_t b = 0;
			std::string keys;
			if (!button(a, b, error) || !arg(a, 1, keys, error)) return {};
			change(b, keys);
			return {};
		});
	command("addKey", "one more key for a button (as the window names a key pressed)", {"button", "key"},
		[this](const Args &a, std::string &error) -> Value {
			size_t b = 0;
			std::string key;
			if (!button(a, b, error) || !arg(a, 1, key, error)) return {};
			if (key.empty())
			{
				error = "no key";
				return {};
			}
			change(b, withKey(session_.bindingTexts()[b], key));
			return {};
		});
	command("reset", "a button's keys back to the package's", {"button"}, [this](const Args &a, std::string &error) -> Value {
		size_t b = 0;
		if (!button(a, b, error)) return {};
		change(b, session_.defaultBindingTexts()[b]);
		return {};
	});
	command("resetAll", "every button's keys back to the package's", {}, [this](const Args &, std::string &) -> Value {
		Json all = parsedObject(saved.get());
		removeMember(all, core.get());
		saved.set(json::write(all));
		apply();
		status.set("Every button has the package's keys");
		return {};
	});

	opensWatch_ = session_.opens.changed.connect([this] {
		core.set(session_.coreName.get());
		apply();
	});
	/* the settings file restoring what was saved */
	savedWatch_ = saved.changed.connect([this] {
		if (!core.get().empty()) apply();
	});
}

bool ControlsViewModel::button(const Args &a, size_t &out, std::string &error) const
{
	int64_t i = 0;
	if (!arg(a, 0, i, error)) return false;
	if (i < 0 || i >= static_cast<int64_t>(session_.buttonNames().size()))
	{
		error = "no button " + std::to_string(i);
		return false;
	}
	out = static_cast<size_t>(i);
	return true;
}

void ControlsViewModel::apply()
{
	const std::vector<std::string> &names = session_.buttonNames();
	std::vector<std::string> texts = session_.defaultBindingTexts();
	texts.resize(names.size());
	const Json all = parsedObject(saved.get());
	if (const Json *mine = all.find(core.get()))
		for (size_t b = 0; b < names.size(); b++)
			if (const Json *keys = mine->find(names[b]); keys != nullptr && keys->kind == Json::Kind::String) texts[b] = keys->text;
	session_.setBindingTexts(texts);
	rows_.reset.emit();
}

void ControlsViewModel::change(size_t b, const std::string &keys)
{
	const std::string &name = session_.buttonNames()[b];
	Json all = parsedObject(saved.get());
	Json mine = all.find(core.get()) != nullptr ? *all.find(core.get()) : Json();
	if (mine.kind != Json::Kind::Object) mine.kind = Json::Kind::Object;
	/* what the package has is no change */
	if (keys == session_.defaultBindingTexts()[b]) removeMember(mine, name);
	else setMember(mine, name, Json::string(keys));
	if (mine.members.empty()) removeMember(all, core.get());
	else setMember(all, core.get(), mine);
	saved.set(json::write(all));
	apply();
	status.set(name + ": " + (keys.empty() ? std::string("no keys") : keys));
}

std::string ControlsViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"button", "keys", "packageKeys", "changed"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value ControlsViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const size_t b = static_cast<size_t>(row);
	const std::vector<std::string> &now = vm_.session_.bindingTexts(), &theirs = vm_.session_.defaultBindingTexts();
	const std::string keys = b < now.size() ? now[b] : std::string(), package = b < theirs.size() ? theirs[b] : std::string();
	switch (column)
	{
	case 0: return vm_.session_.buttonNames()[b];
	case 1: return keys;
	case 2: return package;
	case 3: return keys != package;
	default: return {};
	}
}

} // namespace chimera::vm
