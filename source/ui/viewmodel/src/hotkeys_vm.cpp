#include "chimera/vm/hotkeys_vm.hpp"

#include "chimera/vm/json.hpp"

namespace chimera::vm {

using json::Json;

namespace {
std::vector<std::string> keyList(const std::string &keys)
{
	std::vector<std::string> out;
	size_t at = 0;
	while (at <= keys.size())
	{
		const size_t comma = keys.find(',', at);
		std::string one = keys.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
		while (!one.empty() && one.front() == ' ') one.erase(0, 1);
		while (!one.empty() && one.back() == ' ') one.pop_back();
		if (!one.empty()) out.push_back(one);
		if (comma == std::string::npos) break;
		at = comma + 1;
	}
	return out;
}
} // namespace

HotkeysViewModel::HotkeysViewModel(Dispatcher &dispatcher) : ViewModel("hotkeys", dispatcher)
{
	expose("saved", "the keys changed, as {\"action\": \"keys\"}", saved, true);
	expose("status", "what Hotkeys last had to say", status);
	rows("hotkeys", "one row an action: group, name, its keys, Chimera's keys for it, changed", rows_);

	command("bind", "an action's keys, replaced (\"F, X1 RightShoulder\"; \"\" for none)", {"action", "keys"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name, keys;
			if (!arg(a, 0, name, error) || !arg(a, 1, keys, error)) return {};
			Action *act = find(name);
			if (act == nullptr)
			{
				error = "no action " + name;
				return {};
			}
			change(*act, keys);
			return {};
		});
	command("addKey", "one more key for an action", {"action", "key"}, [this](const Args &a, std::string &error) -> Value {
		std::string name, key;
		if (!arg(a, 0, name, error) || !arg(a, 1, key, error)) return {};
		Action *act = find(name);
		if (act == nullptr || key.empty())
		{
			error = act == nullptr ? "no action " + name : "no key";
			return {};
		}
		for (const std::string &k : keyList(act->keys))
			if (k == key) return {};
		change(*act, act->keys.empty() ? key : act->keys + ", " + key);
		return {};
	});
	command("reset", "an action's keys back to Chimera's", {"action"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		Action *act = find(name);
		if (act == nullptr)
		{
			error = "no action " + name;
			return {};
		}
		change(*act, act->defaults);
		return {};
	});
	command("resetAll", "every action's keys back to Chimera's", {}, [this](const Args &, std::string &) -> Value {
		saved.set("{}");
		apply();
		status.set("Every action has Chimera's keys");
		return {};
	});
	command("run", "runs an action as its key would, pressed and let go", {"action"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		Action *act = find(name);
		if (act == nullptr)
		{
			error = "no action " + name;
			return {};
		}
		act->handler(true);
		act->handler(false);
		return {};
	});
	savedWatch_ = saved.changed.connect([this] { apply(); });
}

void HotkeysViewModel::add(std::string group, std::string name, std::string defaultKeys, Handler handler)
{
	actions_.push_back({std::move(group), std::move(name), defaultKeys, defaultKeys, std::move(handler)});
	apply();
}

HotkeysViewModel::Action *HotkeysViewModel::find(const std::string &name)
{
	for (Action &a : actions_)
		if (a.name == name) return &a;
	return nullptr;
}

void HotkeysViewModel::apply()
{
	Json all;
	std::string error;
	if (!json::parse(saved.get(), all, error) || all.kind != Json::Kind::Object) all.kind = Json::Kind::Object;
	for (Action &a : actions_)
	{
		const Json *mine = all.find(a.name);
		a.keys = mine != nullptr && mine->kind == Json::Kind::String ? mine->text : a.defaults;
	}
	rows_.reset.emit();
}

void HotkeysViewModel::change(Action &a, const std::string &keys)
{
	Json all;
	std::string error;
	if (!json::parse(saved.get(), all, error) || all.kind != Json::Kind::Object) all = Json(), all.kind = Json::Kind::Object;
	for (auto it = all.members.begin(); it != all.members.end(); ++it)
		if (it->first == a.name)
		{
			all.members.erase(it);
			break;
		}
	/* Chimera's keys are no change */
	if (keys != a.defaults) all.members.emplace_back(a.name, Json::string(keys));
	saved.set(json::write(all));
	apply();
	status.set(a.name + ": " + (keys.empty() ? std::string("no keys") : keys));
}

bool HotkeysViewModel::key(const std::string &name, bool down)
{
	bool ran = false;
	for (Action &a : actions_)
	{
		const auto held = std::make_pair(a.name, name);
		if (!down)
		{
			/* let go: whatever it put down comes up, rebound since or not */
			if (down_.erase(held) != 0)
			{
				a.handler(false);
				ran = true;
			}
			continue;
		}
		bool bound = false;
		for (const std::string &k : keyList(a.keys)) bound = bound || k == name;
		if (!bound || down_.count(held) != 0) continue; /* not its key, or the key repeating */
		down_.insert(held);
		a.handler(true);
		ran = true;
	}
	return ran;
}

std::string HotkeysViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"group", "action", "keys", "chimeraKeys", "changed"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value HotkeysViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Action &a = vm_.actions_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return a.group;
	case 1: return a.name;
	case 2: return a.keys;
	case 3: return a.defaults;
	case 4: return a.keys != a.defaults;
	default: return {};
	}
}

} // namespace chimera::vm
