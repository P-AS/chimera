#pragma once
/* Hotkeys: keys that run the window's actions (frame advance, rewind, the
 * branch slots...), as Chimera's do. Each action is named and grouped as
 * Chimera names it and has Chimera's key at first; a person changes them
 * here, and the changes are remembered (`saved`). A key held runs its
 * action's "down", and its "up" when let go. Like Chimera by default, a key
 * that is both a hotkey and a machine's button does both. */

#include <functional>
#include <set>
#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class HotkeysViewModel : public ViewModel
{
public:
	explicit HotkeysViewModel(Dispatcher &dispatcher);

	using Handler = std::function<void(bool down)>;
	/* an action keys can run: its group, its name, its keys at first ("" for
	 * none), and what it does */
	void add(std::string group, std::string name, std::string defaultKeys, Handler handler);
	/* a key went down or up (Chimera's key names): true when it ran any action */
	bool key(const std::string &name, bool down);

	/* the keys changed: {"action": "keys"} */
	Property<std::string> saved{"{}"};
	Property<std::string> status;

private:
	struct Action
	{
		std::string group, name, defaults, keys;
		Handler handler;
	};
	class Rows : public RowModel
	{
	public:
		explicit Rows(HotkeysViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.actions_.size()); }
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		HotkeysViewModel &vm_;
	};

	Action *find(const std::string &name);
	void apply();
	void change(Action &a, const std::string &keys);

	std::vector<Action> actions_;
	Rows rows_{*this};
	/* the actions down now, by the key that put them down */
	std::set<std::pair<std::string, std::string>> down_;
	Connection savedWatch_;
};

} // namespace chimera::vm
