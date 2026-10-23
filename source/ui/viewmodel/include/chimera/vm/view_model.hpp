#pragma once
/* A view model: the state and the actions behind one window, with no window
 * in it. Each one names what it has - its properties, its commands, its lists
 * of rows and its pictures - in one registry, and that registry is the only
 * way in: the window binds to it through one generic adapter, and a Lua
 * script, the MCP server and the tests call the same names. Whatever a window
 * can do, a script can do, because there is nothing else to call. */

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chimera/vm/observable.hpp"
#include "chimera/vm/picture.hpp"
#include "chimera/vm/rows.hpp"
#include "chimera/vm/value.hpp"

namespace chimera::vm {

using Args = std::vector<Value>;

class ViewModel
{
public:
	struct PropertyInfo
	{
		std::string name, help;
		std::function<Value()> get;
		/* empty for a property only the view model changes */
		std::function<bool(const Value &)> set;
		Signal<> *changed;
	};
	struct CommandInfo
	{
		std::string name, help;
		std::vector<std::string> params;
		/* fills `error` and returns nothing when it cannot run */
		std::function<Value(const Args &, std::string &error)> run;
		/* null for a command that can always run */
		Property<bool> *enabled;
	};
	struct RowsInfo
	{
		std::string name, help;
		RowModel *model;
	};
	struct PictureInfo
	{
		std::string name, help;
		Picture *picture;
		/* emitted on the view's thread when a newer frame is waiting */
		Signal<> *changed;
	};

	ViewModel(std::string name, Dispatcher &dispatcher);
	virtual ~ViewModel();
	ViewModel(const ViewModel &) = delete;
	ViewModel &operator=(const ViewModel &) = delete;

	const std::string &name() const { return name_; }
	const std::vector<PropertyInfo> &properties() const { return properties_; }
	const std::vector<CommandInfo> &commands() const { return commands_; }
	const std::vector<RowsInfo> &rowLists() const { return rows_; }
	const std::vector<PictureInfo> &pictures() const { return pictures_; }

	/* By name, for scripts, the MCP server and tests. */
	std::optional<Value> get(std::string_view property) const;
	bool set(std::string_view property, const Value &v, std::string &error);
	std::optional<Value> run(std::string_view command, const Args &args, std::string &error);
	/* a command about to run, by its name: whoever asked for it */
	Signal<std::string> commandRunning;

protected:
	template <class T>
	void expose(std::string name, std::string help, Property<T> &p, bool writable = false)
	{
		PropertyInfo info{std::move(name), std::move(help), [&p] { return toValue(p.get()); }, nullptr, &p.changed};
		if (writable)
			info.set = [&p](const Value &v) {
				T t{};
				if (!fromValue(v, t)) return false;
				p.set(std::move(t));
				return true;
			};
		properties_.push_back(std::move(info));
	}
	void command(std::string name, std::string help, std::vector<std::string> params,
		std::function<Value(const Args &, std::string &)> run, Property<bool> *enabled = nullptr);
	void rows(std::string name, std::string help, RowModel &model);
	void picture(std::string name, std::string help, Picture &picture, Signal<> &changed);

	/* Runs fn on the view's thread, unless this view model is gone by then. */
	void later(std::function<void()> fn);

	/* argument i as T; false (with the error said) when it is missing or not a T */
	template <class T>
	static bool arg(const Args &args, size_t i, T &out, std::string &error)
	{
		if (i >= args.size())
		{
			error = "argument " + std::to_string(i + 1) + " is missing";
			return false;
		}
		if (!fromValue(args[i], out))
		{
			error = "argument " + std::to_string(i + 1) + " cannot be " + describe(args[i]);
			return false;
		}
		return true;
	}

	Dispatcher &dispatcher_;

private:
	std::string name_;
	std::vector<PropertyInfo> properties_;
	std::vector<CommandInfo> commands_;
	std::vector<RowsInfo> rows_;
	std::vector<PictureInfo> pictures_;
	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

} // namespace chimera::vm
