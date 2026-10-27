#include "chimera/vm/project_details_vm.hpp"

#include <algorithm>
#include <cstdio>

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

using json::Json;

namespace {
std::string text(const char *s) { return s != nullptr ? s : ""; }

/* the colour as typed: "#RRGGBB" or "#AARRGGBB", or as Chimera writes
 * it in hex without the mark */
bool readColor(std::string s, uint32_t &out)
{
	if (!s.empty() && s[0] == '#') s.erase(0, 1);
	if (s.size() != 6 && s.size() != 8) return false;
	if (s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
	out = static_cast<uint32_t>(std::stoul(s, nullptr, 16));
	if (s.size() == 6) out |= 0xFF000000u;
	return true;
}

std::string writeColor(uint32_t argb)
{
	char buf[12];
	std::snprintf(buf, sizeof buf, "#%08X", argb);
	return buf;
}
} // namespace

ProjectDetailsViewModel::ProjectDetailsViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("projectDetails", dispatcher), session_(session)
{
	expose("author", "who made the movie", author);
	expose("emulatorVersion", "the emulator's version, as the header says it", emulatorVersion);
	expose("boardName", "the game's board, as the header says it", boardName);
	expose("gameName", "the game's name (the project's title)", gameName);
	expose("core", "the project's core (its package pin decides it; not edited here)", core);
	expose("comments", "the project's comments, a line each", comments);
	expose("defaultAuthor", "the author a new project starts with", defaultAuthor, true);
	expose("showSubtitles", "the subtitles are shown over the picture", showSubtitles, true);
	expose("onScreen", "the subtitles on the frame shown, as [{\"text\", \"x\", \"y\", \"color\"}]", onScreen);
	expose("count", "how many subtitles the project has", count);
	expose("status", "what the project's details last had to say", status);
	rows("subtitles", "one row a subtitle, by frame: frame, fromLeft and fromTop (where, in pixels), duration, color (#AARRGGBB), message", rows_);

	command("headerEntries", "the open project's header, every entry, a line each: its key, a tab, its value (Chimera's "
		"movie.getheader); \"\" with no project", {}, [this](const Args &, std::string &) -> Value {
			std::string out;
			session_.onProjectWait([&](ce_project *p) {
				if (p == nullptr) return;
				for (int32_t i = 0; i < ce_project_header_count(p); i++)
					out += text(ce_project_header_key_at(p, i)) + "\t" + text(ce_project_header_value_at(p, i)) + "\n";
			});
			return out;
		});
	command("subtitleLines", "the open project's subtitles, a line each as the project writes it (Chimera's "
		"movie.getsubtitles)", {}, [this](const Args &, std::string &) -> Value {
			std::string out;
			session_.onProjectWait([&](ce_project *p) {
				if (p == nullptr) return;
				for (int32_t i = 0; i < ce_project_subtitle_count(p); i++) out += text(ce_project_subtitle_at(p, i)) + "\n";
			});
			return out;
		});
	command("reload", "reads the open project's details again", {}, [this](const Args &, std::string &) -> Value {
		load();
		return {};
	});
	command("setHeader", "the header's author, emulator version, board name and game name; makeDefault keeps the "
		"author for new projects", {"author", "emulatorVersion", "boardName", "gameName", "makeDefault"},
		[this](const Args &a, std::string &error) -> Value {
			std::string au, ev, bn, gn;
			bool makeDefault = false;
			if (!arg(a, 0, au, error) || !arg(a, 1, ev, error) || !arg(a, 2, bn, error) || !arg(a, 3, gn, error)) return {};
			if (a.size() > 4 && !arg(a, 4, makeDefault, error)) return {};
			bool open = false;
			session_.onProjectWait([&](ce_project *p) {
				if (p == nullptr) return;
				open = true;
				ce_project_header_set(p, "Author", au.c_str());
				ce_project_header_set(p, "emuVersion", ev.c_str());
				ce_project_header_set(p, "BoardName", bn.c_str());
				ce_project_header_set(p, "GameName", gn.c_str());
				ce_project_set_title(p, gn.c_str());
			});
			if (!open)
			{
				error = "no project is open";
				return {};
			}
			if (makeDefault) defaultAuthor.set(au);
			session_.markChanged();
			load();
			status.set("The header is changed; it is kept when the project is saved");
			return {};
		});
	command("setComments", "the comments, a line each", {"comments"}, [this](const Args &a, std::string &error) -> Value {
		std::string c;
		if (!arg(a, 0, c, error)) return {};
		/* blank lines are not comments, as Chimera reads them */
		std::string kept;
		size_t at = 0;
		while (at <= c.size())
		{
			const size_t end = c.find('\n', at);
			std::string line = c.substr(at, end == std::string::npos ? std::string::npos : end - at);
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.find_first_not_of(" \t") != std::string::npos) kept += (kept.empty() ? "" : "\n") + line;
			if (end == std::string::npos) break;
			at = end + 1;
		}
		bool open = false;
		session_.onProjectWait([&](ce_project *p) {
			if (p == nullptr) return;
			open = true;
			ce_project_set_description(p, kept.c_str());
		});
		if (!open)
		{
			error = "no project is open";
			return {};
		}
		session_.markChanged();
		load();
		return {};
	});
	command("addSubtitle", "a subtitle: from a frame, at x and y, for a number of frames, in a colour (#RRGGBB or "
		"#AARRGGBB), saying a message", {"frame", "x", "y", "duration", "color", "message"},
		[this](const Args &a, std::string &error) -> Value {
			Subtitle sub;
			if (!subtitleArgs(a, 0, sub, error)) return {};
			if (!session_.hasProject.get())
			{
				error = "no project is open";
				return {};
			}
			subtitles_.push_back(sub);
			storeSubtitles();
			return {};
		});
	command("editSubtitle", "a subtitle, by its row, made this one", {"index", "frame", "x", "y", "duration", "color", "message"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			Subtitle sub;
			if (!arg(a, 0, i, error) || !subtitleArgs(a, 1, sub, error)) return {};
			if (i < 0 || i >= static_cast<int64_t>(subtitles_.size()))
			{
				error = "no subtitle " + std::to_string(i);
				return {};
			}
			subtitles_[static_cast<size_t>(i)] = sub;
			storeSubtitles();
			return {};
		});
	command("removeSubtitle", "a subtitle, by its row, taken out", {"index"}, [this](const Args &a, std::string &error) -> Value {
		int64_t i = 0;
		if (!arg(a, 0, i, error)) return {};
		if (i < 0 || i >= static_cast<int64_t>(subtitles_.size()))
		{
			error = "no subtitle " + std::to_string(i);
			return {};
		}
		subtitles_.erase(subtitles_.begin() + i);
		storeSubtitles();
		return {};
	});

	command("subtitleOn", "the first subtitle that starts on a frame, as Chimera's Add Subtitle edits it: "
		"{\"index\", \"x\", \"y\", \"duration\", \"color\", \"message\"}; \"\" for none", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			for (size_t i = 0; i < subtitles_.size(); i++)
				if (subtitles_[i].frame == frame)
				{
					json::Json o;
					o.kind = json::Json::Kind::Object;
					o.members.emplace_back("index", json::Json::integer(static_cast<long long>(i)));
					o.members.emplace_back("x", json::Json::integer(subtitles_[i].x));
					o.members.emplace_back("y", json::Json::integer(subtitles_[i].y));
					o.members.emplace_back("duration", json::Json::integer(subtitles_[i].duration));
					o.members.emplace_back("color", json::Json::string(writeColor(subtitles_[i].color)));
					o.members.emplace_back("message", json::Json::string(subtitles_[i].message));
					return json::write(o);
				}
			return std::string();
		});

	opensWatch_ = session_.opens.changed.connect([this] { load(); });
	/* the session says a project is open after the machine is */
	projectWatch_ = session_.hasProject.changed.connect([this] { load(); });
	frameWatch_ = session_.frame.changed.connect([this] { showFrame(); });
	showWatch_ = showSubtitles.changed.connect([this] { showFrame(); });
}

bool ProjectDetailsViewModel::subtitleArgs(const Args &a, size_t from, Subtitle &out, std::string &error) const
{
	std::string color;
	if (!arg(a, from, out.frame, error) || !arg(a, from + 1, out.x, error) || !arg(a, from + 2, out.y, error) ||
		!arg(a, from + 3, out.duration, error) || !arg(a, from + 4, color, error) || !arg(a, from + 5, out.message, error))
		return false;
	if (out.frame < 0 || out.duration < 1)
	{
		error = "a subtitle starts at a frame, 0 or after, and lasts a frame or more";
		return false;
	}
	if (!readColor(color, out.color))
	{
		error = color + " is not a colour: #RRGGBB or #AARRGGBB";
		return false;
	}
	if (out.message.find('\n') != std::string::npos)
	{
		error = "a subtitle is one line";
		return false;
	}
	return true;
}

void ProjectDetailsViewModel::load()
{
	std::string au, ev, bn, gn, co, cm;
	std::vector<Subtitle> subs;
	/* a project just made, never saved, has the default author, as Chimera's new projects do */
	const bool fresh = session_.hasProject.get() && session_.projectPath.get().empty();
	const std::string byDefault = defaultAuthor.get();
	session_.onProjectWait([&](ce_project *p) {
		if (p == nullptr) return;
		if (fresh && text(ce_project_header_get(p, "Author")).empty() && !byDefault.empty())
			ce_project_header_set(p, "Author", byDefault.c_str());
		au = text(ce_project_header_get(p, "Author"));
		ev = text(ce_project_header_get(p, "emuVersion"));
		bn = text(ce_project_header_get(p, "BoardName"));
		gn = text(ce_project_title(p));
		co = text(ce_project_core_name(p));
		cm = text(ce_project_description(p));
		for (int32_t i = 0; i < ce_project_subtitle_count(p); i++)
		{
			const char *line = ce_project_subtitle_at(p, i);
			if (line == nullptr) continue;
			ce_subtitle_fields f{};
			std::vector<char> message(std::char_traits<char>::length(line) + 1);
			if (ce_subtitle_parse_line(line, &f, message.data(), message.size()) < 0) continue;
			subs.push_back({f.frame, f.x, f.y, f.duration, f.color, message.data()});
		}
	});
	std::stable_sort(subs.begin(), subs.end(), [](const Subtitle &a, const Subtitle &b) { return a.frame < b.frame; });
	author.set(au);
	emulatorVersion.set(ev);
	boardName.set(bn);
	gameName.set(gn);
	core.set(co);
	comments.set(cm);
	subtitles_ = std::move(subs);
	count.set(static_cast<int64_t>(subtitles_.size()));
	rows_.reset.emit();
	showFrame();
}

void ProjectDetailsViewModel::storeSubtitles()
{
	/* kept by frame, as Chimera sorts them */
	std::stable_sort(subtitles_.begin(), subtitles_.end(), [](const Subtitle &a, const Subtitle &b) { return a.frame < b.frame; });
	std::vector<std::string> lines;
	for (const Subtitle &s : subtitles_)
	{
		const ce_subtitle_fields f{static_cast<int32_t>(s.frame), static_cast<int32_t>(s.x), static_cast<int32_t>(s.y),
			static_cast<int32_t>(s.duration), s.color};
		const int64_t len = ce_subtitle_format_line(&f, s.message.c_str(), nullptr, 0);
		std::vector<char> buf(static_cast<size_t>(std::max<int64_t>(len, 0)) + 1);
		ce_subtitle_format_line(&f, s.message.c_str(), buf.data(), buf.size());
		lines.emplace_back(buf.data());
	}
	session_.onProjectWait([&](ce_project *p) {
		if (p == nullptr) return;
		while (ce_project_subtitle_count(p) > 0) ce_project_subtitle_remove(p, 0);
		for (const std::string &l : lines) ce_project_subtitle_add(p, l.c_str());
	});
	count.set(static_cast<int64_t>(subtitles_.size()));
	rows_.reset.emit();
	showFrame();
	session_.markChanged();
	status.set(std::to_string(subtitles_.size()) + (subtitles_.size() == 1 ? " subtitle" : " subtitles") +
		"; kept when the project is saved");
}

void ProjectDetailsViewModel::showFrame()
{
	Json shown;
	shown.kind = Json::Kind::Array;
	if (showSubtitles.get())
	{
		const int64_t f = session_.frame.get();
		for (const Subtitle &s : subtitles_)
			if (f >= s.frame && f < s.frame + s.duration)
			{
				Json one;
				one.kind = Json::Kind::Object;
				one.members.emplace_back("text", Json::string(s.message));
				one.members.emplace_back("x", Json::integer(s.x));
				one.members.emplace_back("y", Json::integer(s.y));
				one.members.emplace_back("color", Json::string(writeColor(s.color | 0xFF000000u)));
				shown.items.push_back(one);
			}
	}
	const std::string text = json::write(shown);
	if (text != onScreen.get()) onScreen.set(text);
}

std::string ProjectDetailsViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"frame", "fromLeft", "fromTop", "duration", "color", "message"};
	return column >= 0 && column < 6 ? names[column] : "";
}

Value ProjectDetailsViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Subtitle &s = vm_.subtitles_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return s.frame;
	case 1: return s.x;
	case 2: return s.y;
	case 3: return s.duration;
	case 4: return writeColor(s.color);
	case 5: return s.message;
	default: return {};
	}
}

} // namespace chimera::vm
