#include "chimera/vm/themes_vm.hpp"

#include <algorithm>
#include <cctype>
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
	command("refresh", "reads the themes folder again (Reload Themes)", {}, [this](const Args &, std::string &) -> Value {
		refresh();
		return count.get();
	});
	expose("failures", "the files that would not load, a line each with why", failures);
	expose("names", "every theme's name, in the list's order (a JSON array)", names);
	command("about", "what a theme says of itself, by name: {\"name\", \"about\" (its lines joined by \" / \"), \"author\", "
		"\"origin\", \"builtIn\", \"dark\"}", {"name"}, [this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			for (size_t i = 0; i < themes_.size(); i++)
				if (themes_[i].name == name)
				{
					Json o;
					o.kind = Json::Kind::Object;
					std::string about;
					fromValue(rows_.cell(static_cast<int64_t>(i), 9), about);
					o.members.emplace_back("name", Json::string(themes_[i].name));
					o.members.emplace_back("about", Json::string(about));
					o.members.emplace_back("author", Json::string(themes_[i].author));
					o.members.emplace_back("origin", Json::string(themes_[i].origin));
					o.members.emplace_back("builtIn", Json::boolean_(themes_[i].origin == "built in"));
					o.members.emplace_back("dark", Json::boolean_(themes_[i].dark));
					return json::write(o);
				}
			error = "no theme called " + name;
			return {};
		});
	command("colorsOf", "a theme's colours, by name, as {\"Role\": \"#RRGGBB\"}, for a preview of one that is not on", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			for (const Theme &t : themes_)
				if (t.name == name) return json::write(t.colors);
			error = "no theme called " + name;
			return {};
		});
	command("writeCopy", "a theme written into the themes folder as a file to edit (Write a Copy to Edit): named \"<name> copy\" "
		"(\"copy 2\"...), never over a file that is there; its colours as given (the window paints the desktop's own), or the "
		"theme's. Says where it went", {"name", "colors"}, [this](const Args &a, std::string &error) -> Value {
			std::string name, given;
			if (!arg(a, 0, name, error)) return {};
			if (a.size() > 1 && !arg(a, 1, given, error)) return {};
			const auto src = std::find_if(themes_.begin(), themes_.end(), [&](const Theme &t) { return t.name == name; });
			if (src == themes_.end())
			{
				error = "no theme called " + name;
				return {};
			}
			Json colours = src->colors;
			if (!given.empty())
			{
				Json g;
				std::string why;
				if (json::parse(given, g, why) && g.kind == Json::Kind::Object) colours = g;
			}
			const fs::path dir(folder.get());
			std::string copy;
			fs::path path;
			for (int n = 1;; n++)
			{
				copy = n == 1 ? src->name + " copy" : src->name + " copy " + std::to_string(n);
				std::string file;
				for (char c : copy) file += std::string("<>:\"/\\|?*").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32 ? '_' : c;
				path = dir / (file + ".chimeraTheme.json");
				std::error_code ec;
				if (std::none_of(themes_.begin(), themes_.end(), [&](const Theme &t) { return t.name == copy; }) && !fs::exists(path, ec)) break;
			}
			/* as Chimera's ThemeFile writes one */
			std::string text = "{\n\t\"name\": " + json::quote(copy) + ",\n\t\"description\": " + json::quote(src->description) + ",\n";
			if (!src->author.empty()) text += "\t\"author\": " + json::quote(src->author) + ",\n";
			text += std::string("\t\"dark\": ") + (src->dark ? "true" : "false") + ",\n\t\"colors\": {\n";
			for (size_t i = 0; i < colours.members.size(); i++)
				text += "\t\t" + json::quote(colours.members[i].first) + ": " + json::quote(colours.members[i].second.text) +
					(i + 1 < colours.members.size() ? ",\n" : "\n");
			text += "\t}\n}\n";
			std::error_code ec;
			fs::create_directories(dir, ec);
			std::ofstream out(path, std::ios::binary);
			out << text;
			if (!out)
			{
				error = "Could not write " + path.string();
				return {};
			}
			out.close();
			refresh();
			status.set("Written to " + path.string() + ". Change the colours and the name inside it, then Reload Themes.");
			return copy;
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
		std::vector<Theme> fromFiles;
		for (const fs::path &f : files)
		{
			std::ifstream in(f, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			Theme t;
			std::string why;
			/* a file's theme by the name of one already there stays the first */
			if (!read(text, f.filename().string(), t, why)) failed.push_back(f.filename().string() + ": " + why);
			else if (std::none_of(themes_.begin(), themes_.end(), [&](const Theme &o) { return o.name == t.name; }) &&
				std::none_of(fromFiles.begin(), fromFiles.end(), [&](const Theme &o) { return o.name == t.name; }))
				fromFiles.push_back(std::move(t));
		}
		/* the built-in ones first, as Chimera has always listed them; then the files by name, as sixty themes in the
		 * order of their file names would be in nobody's order */
		auto lowered = [](std::string s) {
			for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		};
		std::stable_sort(fromFiles.begin(), fromFiles.end(), [&](const Theme &x, const Theme &y) { return lowered(x.name) < lowered(y.name); });
		for (Theme &t : fromFiles) themes_.push_back(std::move(t));
	}
	count.set(static_cast<int64_t>(themes_.size()));
	{
		Json list;
		list.kind = Json::Kind::Array;
		for (const Theme &t : themes_) list.items.push_back(Json::string(t.name));
		names.set(json::write(list));
	}
	/* Chimera's line: how many, where from, and what would not load */
	std::string text = std::to_string(themes_.size()) + " themes; files are read from " + folder.get();
	if (!failed.empty()) text += ". " + std::to_string(failed.size()) + (failed.size() == 1 ? " file" : " files") + " would not load - why?";
	std::string lines;
	for (const std::string &f : failed) lines += (lines.empty() ? "" : "\n\n") + f;
	failures.set(lines);
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
	static const char *const names[] = {"name", "author", "description", "dark", "origin", "windowColor", "textColor", "selectionColor", "buttonColor",
		"about", "full", "builtIn"};
	return column >= 0 && column < 12 ? names[column] : "";
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
	/* Chimera's box under the preview: a poem's lines joined by " / "; the list's tooltip has all of it */
	case 9:
	{
		std::string d;
		for (size_t i = 0; i < t.description.size(); i++)
		{
			if (t.description[i] == '\r') continue;
			d += t.description[i] == '\n' ? std::string(" / ") : std::string(1, t.description[i]);
		}
		return d;
	}
	case 10: return t.description;
	case 11: return t.origin == "built in";
	default: return {};
	}
}

} // namespace chimera::vm
