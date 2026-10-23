#include "chimera/vm/help_vm.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#include "chimera/engine.h"
#include "chimera/vm/european_clock.hpp"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace {
std::string readAll(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

HelpViewModel::HelpViewModel(Dispatcher &dispatcher, SessionViewModel &session, SettingsViewModel &settings)
	: ViewModel("help", dispatcher), session_(session), settings_(settings)
{
	/* the build: the engine's commit, as ce_build_info says it */
	{
		json::Json info;
		std::string err;
		std::string commit = "unknown";
		if (json::parse(ce_build_info(), info, err))
			if (const json::Json *c = info.find("commit")) commit = c->text;
		buildName.set("Qt prototype, commit " + commit);
	}
	expose("buildName", "this build, as a report names it", buildName);
	expose("siteUrl", "Chimera's site", siteUrl);
	expose("formUrl", "where an issue is filed", formUrl);
	expose("osName", "the computer's system, as the window's toolkit names it", osName, true);
	expose("reportText", "the report made last, its first lines filled in", reportText);
	expose("reportFiles", "the files worth attaching to it, a line each", reportFiles);
	expose("status", "what Help last had to say", status);

	command("versionReport", "the build and the core open, in the words a bug report asks for them (Chimera's Copy Version Info)",
		{}, [this](const Args &, std::string &) -> Value {
			std::string text = "**Chimera build:** " + buildName.get();
			const std::string core = coreAndVersion();
			if (!core.empty()) text += "\n**Core and version:** " + core;
			return text;
		});
	command("makeReport", "fills in a report's first lines from this session and lists the files worth attaching, as "
		"Chimera's Report an Issue", {}, [this](const Args &, std::string &) -> Value {
			std::vector<std::string> fields;
			fields.push_back("**Chimera build:** " + buildName.get());
			const std::string core = coreAndVersion();
			fields.push_back("**Core and version:** " + (core.empty() ? std::string("<!-- no core was running -->") : core));
			const std::string os = osName.get().empty() ? "<!-- type your operating system here -->" : osName.get();
			fields.push_back("**OS and GPU:** " + os + ", <!-- type your graphics card here -->");
			std::string cpu = ce_host_cpu_name() != nullptr ? ce_host_cpu_name() : "";
			if (cpu.empty()) cpu = "<!-- type your processor here -->";
			if (const unsigned threads = std::thread::hardware_concurrency(); threads > 0) cpu += " (" + std::to_string(threads) + " threads)";
			const uint64_t memory = ce_host_memory_total();
			char gb[32];
			std::snprintf(gb, sizeof gb, "%.1f GB of memory", static_cast<double>(memory) / static_cast<double>(1ull << 30));
			fields.push_back("**CPU and memory:** " + cpu + ", " +
				(memory == 0 ? std::string("<!-- type how much memory your computer has here -->") : std::string(gb)));
			/* the project's files: one, the game, on the line itself */
			std::string game = "**Game:**";
			if (!session_.hasProject.get())
				game += " <!-- no project was open -->";
			else
			{
				/* the open project's own list: slot and file name */
				std::vector<std::pair<std::string, std::string>> files;
				session_.onProjectWait([&](ce_project *p) {
					if (p == nullptr) return;
					for (int32_t i = 0; i < ce_project_file_count(p); i++)
					{
						const char *slot = ce_project_file_slot(p, i), *name = ce_project_file_name(p, i);
						files.emplace_back(slot != nullptr ? slot : "", name != nullptr ? name : "");
					}
				});
				auto named = [](const std::pair<std::string, std::string> &f, bool alone) {
					const std::string name = std::filesystem::path(f.second).filename().string();
					return alone && f.first == "game" ? name : f.first + ": " + name;
				};
				if (files.empty()) game += " the project has no files of its own";
				else if (files.size() == 1) game += " " + named(files[0], true);
				else
					for (const auto &f : files) game += "\n- " + named(f, false);
			}
			fields.push_back(game);
			std::string settings = "**Renderer and non-default settings:**";
			if (!session_.isOpen.get())
				settings += " <!-- no core was running -->";
			else
			{
				const auto changed = settings_.changedSettings();
				if (changed.empty()) settings += " every setting is at its default";
				for (const auto &c : changed) settings += "\n- " + c.label + ": " + c.value + " (default: " + c.standard + ")";
			}
			fields.push_back(settings);
			std::string text;
			for (const std::string &f : fields) text += (text.empty() ? "" : "\n\n") + f;
			reportText.set(text);
			/* the files: the project, and the sandbox's log where the program runs */
			std::string list;
			std::error_code ec;
			if (session_.hasProject.get() && std::filesystem::is_regular_file(session_.projectPath.get(), ec)) list = session_.projectPath.get();
			const std::filesystem::path log = std::filesystem::current_path(ec) / "minibox-diag.log";
			if (std::filesystem::is_regular_file(log, ec)) list += (list.empty() ? "" : "\n") + log.string();
			reportFiles.set(list);
			return text;
		});
	command("saveReport", "the report, the game's picture (a PNG file, \"\" for none) and the files to attach, in one zip",
		{"zip", "picture"}, [this](const Args &a, std::string &error) -> Value {
			std::string zip, picture;
			if (!arg(a, 0, zip, error)) return {};
			if (a.size() > 1 && !arg(a, 1, picture, error)) return {};
			std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
			std::string text = reportText.get();
			text += "\n";
			files.emplace_back("report.md", std::vector<uint8_t>(text.begin(), text.end()));
			std::error_code ec;
			if (!picture.empty() && std::filesystem::is_regular_file(picture, ec))
			{
				const std::string png = readAll(picture);
				files.emplace_back("game.png", std::vector<uint8_t>(png.begin(), png.end()));
			}
			std::vector<std::string> unread;
			size_t at = 0;
			const std::string list = reportFiles.get();
			while (at < list.size())
			{
				const size_t end = list.find('\n', at);
				const std::string path = list.substr(at, end == std::string::npos ? std::string::npos : end - at);
				at = end == std::string::npos ? list.size() : end + 1;
				if (path.empty()) continue;
				std::ifstream in(path, std::ios::binary);
				if (!in)
				{
					unread.push_back(path);
					continue;
				}
				const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				std::string name = std::filesystem::path(path).filename().string();
				/* two files of one name both go in */
				for (int n = 2; std::any_of(files.begin(), files.end(), [&](const auto &f) { return f.first == name; }); n++)
					name = std::filesystem::path(path).stem().string() + " (" + std::to_string(n) + ")" + std::filesystem::path(path).extension().string();
				files.emplace_back(name, std::vector<uint8_t>(bytes.begin(), bytes.end()));
			}
			const std::vector<uint8_t> bytes = storedZip(files);
			std::ofstream out(zip, std::ios::binary);
			out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!out)
			{
				error = "could not write " + zip;
				return {};
			}
			/* past what an issue takes, as Chimera warns */
			constexpr uint64_t kLimit = 25ull * 1024 * 1024;
			std::string said = "Saved " + zip;
			if (bytes.size() > kLimit) said += "; it is larger than the 25 MB an issue takes";
			for (const std::string &u : unread) said += "; could not read " + u;
			status.set(said);
			return static_cast<int64_t>(bytes.size());
		});
}

std::string HelpViewModel::coreAndVersion() const
{
	if (!session_.isOpen.get() || session_.coreName.get().empty()) return {};
	/* the version as the Cores window words it: its date, then the
	 * package's version, eight characters */
	std::string config;
	const char *perr = nullptr;
	if (ce_package *p = ce_package_open(session_.packagePath().c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry(p, "waterbox.config", &len)) config.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
		ce_package_free(p);
	}
	json::Json cfg;
	std::string err, version, dated;
	if (json::parse(config, cfg, err))
	{
		if (const json::Json *v = cfg.find("version")) version = v->text;
		int64_t when = 0;
		if (const json::Json *m = cfg.find("versionDate"); m != nullptr && parseIsoTime(m->text, when)) dated = europeanTime(when);
	}
	if (const size_t plus = version.find('+'); plus != std::string::npos) version.erase(plus);
	if (version.size() > 8) version.erase(8);
	std::string text = session_.coreName.get();
	if (!dated.empty()) text += " " + dated + (version.empty() ? "" : " (" + version + ")");
	else if (!version.empty()) text += " " + version;
	return text;
}

} // namespace chimera::vm
