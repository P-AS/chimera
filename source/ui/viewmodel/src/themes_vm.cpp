#include "chimera/vm/themes_vm.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace chimera::vm {

using json::Json;
namespace fs = std::filesystem;

namespace {
bool isColour(const std::string &s)
{
	/* a desktop colour by its name, which the window knows ("system:Control") */
	if (s.rfind("system:", 0) == 0) return s.size() > 7;
	if (s.size() != 7 && s.size() != 9) return false;
	if (s[0] != '#') return false;
	return s.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
}
} // namespace

ThemesViewModel::ThemesViewModel(Dispatcher &dispatcher) : ViewModel("themes", dispatcher)
{
	expose("folder", "the folder themes are read from", folder, true);
	expose("current", "the theme chosen, by name", current, true);
	expose("colors", "the chosen theme's colours, {\"Role\": \"#RRGGBB\"}", colors);
	expose("dark", "the chosen theme is a dark one", dark);
	expose("count", "how many themes there are", count);
	expose("status", "what Themes last had to say", status);
	rows("themes", "one row a theme: name, author, description, dark, where it comes from, and four of its colours "
		"(window, text, selection, buttons)", rows_);

	command("choose", "the theme to use, by name", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		const bool known = std::any_of(themes_.begin(), themes_.end(), [&](const Theme &t) { return t.name == name; });
		if (!known)
		{
			error = "no theme called " + name;
			return {};
		}
		current.set(name);
		return {};
	});
	command("refresh", "reads the themes folder again", {}, [this](const Args &, std::string &) -> Value {
		refresh();
		return count.get();
	});
	folderWatch_ = folder.changed.connect([this] { refresh(); });
	currentWatch_ = current.changed.connect([this] { show(); });
}

bool ThemesViewModel::read(const std::string &text, const std::string &origin, Theme &out, std::string &error)
{
	Json j;
	if (!json::parse(text, j, error)) return false;
	const Json *name = j.find("name"), *colours = j.find("colors");
	if (name == nullptr || name->kind != Json::Kind::String || name->text.empty())
	{
		error = "it has no name";
		return false;
	}
	if (colours == nullptr || colours->kind != Json::Kind::Object)
	{
		error = "it has no colours";
		return false;
	}
	Theme t;
	t.name = name->text;
	t.origin = origin;
	if (const Json *a = j.find("author")) t.author = a->text;
	if (const Json *d = j.find("description")) t.description = d->text;
	if (const Json *d = j.find("dark")) t.dark = d->boolean;
	t.colors.kind = Json::Kind::Object;
	for (const auto &[role, value] : colours->members)
		if (value.kind == Json::Kind::String && isColour(value.text)) t.colors.members.emplace_back(role, value);
	out = std::move(t);
	return true;
}

bool ThemesViewModel::addBuiltIn(const std::string &text, std::string &error)
{
	Theme t;
	if (!read(text, "built in", t, error)) return false;
	builtIn_.push_back(std::move(t));
	refresh();
	return true;
}

void ThemesViewModel::refresh()
{
	themes_ = builtIn_;
	std::vector<std::string> failed;
	std::error_code ec;
	if (!folder.get().empty() && fs::is_directory(folder.get(), ec))
	{
		std::vector<fs::path> files;
		for (const auto &e : fs::directory_iterator(folder.get(), ec))
		{
			const std::string n = e.path().filename().string();
			if (n.size() > 18 && n.compare(n.size() - 18, 18, ".chimeraTheme.json") == 0) files.push_back(e.path());
		}
		std::sort(files.begin(), files.end());
		for (const fs::path &f : files)
		{
			std::ifstream in(f, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			Theme t;
			std::string why;
			/* a file's theme by the name of one already there stays the first */
			if (!read(text, f.filename().string(), t, why)) failed.push_back(f.filename().string() + " (" + why + ")");
			else if (std::none_of(themes_.begin(), themes_.end(), [&](const Theme &o) { return o.name == t.name; }))
				themes_.push_back(std::move(t));
		}
	}
	count.set(static_cast<int64_t>(themes_.size()));
	std::string text = std::to_string(themes_.size()) + (themes_.size() == 1 ? " theme" : " themes");
	if (!failed.empty())
	{
		text += "; not read:";
		for (const std::string &f : failed) text += " " + f;
	}
	status.set(text);
	rows_.reset.emit();
	show();
}

void ThemesViewModel::show()
{
	for (const Theme &t : themes_)
		if (t.name == current.get())
		{
			dark.set(t.dark);
			colors.set(json::write(t.colors));
			return;
		}
	/* a theme no longer there: the colours stay until another is chosen */
}

std::string ThemesViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "author", "description", "dark", "origin", "windowColor", "textColor", "selectionColor", "buttonColor"};
	return column >= 0 && column < 9 ? names[column] : "";
}

Value ThemesViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Theme &t = vm_.themes_[static_cast<size_t>(row)];
	auto colour = [&](const char *role) {
		const Json *c = t.colors.find(role);
		return c != nullptr ? c->text : std::string();
	};
	switch (column)
	{
	case 0: return t.name;
	case 1: return t.author;
	case 2:
	{
		/* a line, for a list */
		std::string d = t.description;
		for (char &ch : d)
			if (ch == '\n' || ch == '\r') ch = ' ';
		return d;
	}
	case 3: return t.dark;
	case 4: return t.origin;
	case 5: return colour("WindowBackground");
	case 6: return colour("WindowText");
	case 7: return colour("Selection");
	case 8: return colour("ButtonBackground");
	default: return {};
	}
}

} // namespace chimera::vm
