#include "chimera/vm/new_project_vm.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>

#include "chimera/engine.h"

namespace chimera::vm {

using json::Json;
namespace fs = std::filesystem;

namespace {
std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string baseName(const std::string &path)
{
	return fs::path(path).filename().string();
}

/* a file's kind: its extension, lower case, no dot ("" for none) */
std::string kindOf(const std::string &name)
{
	const std::string ext = fs::path(name).extension().string();
	return lower(ext.empty() ? ext : ext.substr(1));
}

std::string entryText(ce_package *p, const char *name)
{
	uint64_t len = 0;
	const uint8_t *d = ce_package_entry(p, name, &len);
	return d != nullptr ? std::string(reinterpret_cast<const char *>(d), static_cast<size_t>(len)) : std::string();
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
} // namespace

NewProjectViewModel::NewProjectViewModel(Dispatcher &dispatcher, SessionViewModel &session, CoresViewModel &cores)
	: ViewModel("newProject", dispatcher), session_(session), cores_(cores)
{
	expose("coreNames", "the installed cores, a line each", coreNames);
	expose("core", "the core chosen", core);
	expose("machines", "the chosen package's machines, a line each (\"\" for a package of one)", machines);
	expose("machine", "the machine chosen, by its place in machines", machine);
	expose("title", "the project's title (the first game file's name when empty)", title, true);
	expose("problems", "what keeps the project from being made, a line each; \"\" when ready", problems);
	expose("ready", "the project can be made", ready);
	expose("status", "what New Project last had to say", status);
	form_.listMachineSetting(false);
	rows("slots", "the kinds of file the project holds: id, title, min, max, formats, help, exposed, files, count, filter", slotRows_);
	rows("settings", "the settings that apply, as the Settings window lists them", settingRows_);
	rows("firmware", "the firmware the machine needs: id, name, the file, how it was found, missing", firmwareRows_);
	expose("importMenu", "the movie import's menu item (\"\" when the core imports none)", importMenu);
	expose("importMovieLabel", "what the movie to import is", importMovieLabel);
	expose("importMovieFilter", "the kinds of file it comes in, for a file dialog", importMovieFilter);
	expose("importMovie", "the movie picked to import", importMovie);
	expose("importNotes", "the notes of the core's answer, a line each", importNotes);
	expose("imported", "the project will take the imported movie's inputs", imported);
	rows("importFiles", "the files the import reads: option, label, filter, required, more than one, the files picked", importFileRows_);
	rows("importOptions", "the import's options: name, label, ticked", importOptionRows_);
	command("importSetMovie", "the movie to import", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string path;
		if (!arg(a, 0, path, error)) return {};
		std::error_code ec;
		if (import_.kind != Json::Kind::Object || !fs::is_regular_file(path, ec))
		{
			error = import_.kind != Json::Kind::Object ? "this core imports no movies" : path + " is not a file";
			return {};
		}
		importMovie.set(path);
		return {};
	});
	command("importAddFile", "a file the import reads, for one of its inputs", {"option", "path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string option, path;
			if (!arg(a, 0, option, error) || !arg(a, 1, path, error)) return {};
			const Json *input = importInput(option);
			std::error_code ec;
			if (input == nullptr || !fs::is_regular_file(path, ec))
			{
				error = input == nullptr ? "the import has no input " + option : path + " is not a file";
				return {};
			}
			const Json *many = input->find("multiple");
			std::vector<std::string> &list = importPicked_[option];
			if (many == nullptr || !many->boolean) list.clear();
			list.push_back(path);
			importFileRows_.reset.emit();
			return {};
		});
	command("importRemoveFile", "takes a file out of an import input", {"option", "index"},
		[this](const Args &a, std::string &error) -> Value {
			std::string option;
			int64_t i = 0;
			if (!arg(a, 0, option, error) || !arg(a, 1, i, error)) return {};
			auto it = importPicked_.find(option);
			if (it == importPicked_.end() || i < 0 || i >= static_cast<int64_t>(it->second.size()))
			{
				error = "no such file";
				return {};
			}
			it->second.erase(it->second.begin() + static_cast<long>(i));
			importFileRows_.reset.emit();
			return {};
		});
	command("importSetOption", "ticks an import option, or not", {"name", "on"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		bool on = false;
		if (!arg(a, 0, name, error) || !arg(a, 1, on, error)) return {};
		importOptions_[name] = on;
		importOptionRows_.reset.emit();
		return {};
	});
	command("importRun", "asks the core what the movie amounts to, and fills the form with its answer", {},
		[this](const Args &, std::string &error) -> Value {
			if (import_.kind != Json::Kind::Object || importMovie.get().empty())
			{
				error = import_.kind != Json::Kind::Object ? "this core imports no movies" : "pick the movie first";
				return {};
			}
			/* each picked file under its own name, its input's option naming it
			 * (a multiple input's names joined); options only when ticked */
			std::vector<std::pair<std::string, std::string>> mounts;
			Json settings;
			settings.kind = Json::Kind::Object;
			if (const Json *files = import_.find("files"))
				for (const Json &f : files->items)
				{
					const Json *opt = f.find("option");
					const std::string option = opt != nullptr ? opt->text : "";
					const auto it = importPicked_.find(option);
					const bool none = it == importPicked_.end() || it->second.empty();
					if (none)
					{
						if (const Json *req = f.find("required"); req != nullptr && req->boolean)
						{
							const Json *label = f.find("label");
							error = (label != nullptr ? label->text : option) + " is needed";
							return {};
						}
						continue;
					}
					std::string names;
					const Json *sep = f.find("separator");
					for (const std::string &p : it->second)
					{
						const std::string name = baseName(p);
						mounts.emplace_back(name, p);
						names += (names.empty() ? "" : (sep != nullptr ? sep->text : std::string(";"))) + name;
					}
					if (!option.empty()) setMember(settings, option, Json::string(names));
				}
			for (const auto &[name, on] : importOptions_)
				if (on) setMember(settings, name, Json::boolean_(true));
			status.set("Asking " + core.get() + " about " + baseName(importMovie.get()) + "...");
			auto done = [this](std::string answer, std::string why) {
				if (answer.empty())
				{
					status.set("Not imported: " + (why.empty() ? core.get() + " gave no answer" : why));
					return;
				}
				applyImport(answer);
			};
			if (importRunner_)
				importRunner_(package_, importMovie.get(), mounts, json::write(settings), done);
			else
			{
				std::vector<const char *> names{"movie"}, paths{importMovie.get().c_str()};
				for (const auto &[n, p] : mounts)
				{
					names.push_back(n.c_str());
					paths.push_back(p.c_str());
				}
				std::vector<const uint8_t *> datas(names.size(), nullptr);
				std::vector<uint64_t> lens(names.size(), 0);
				uint64_t len = 0;
				const char *err = nullptr;
				const std::string settingsText = json::write(settings);
				const char *answer = ce_import_movie(package_.c_str(), nullptr, 0, nullptr, settingsText.c_str(), nullptr, nullptr, nullptr, 0,
					names.data(), datas.data(), lens.data(), paths.data(), static_cast<int32_t>(names.size()), &len, &err);
				done(answer != nullptr ? std::string(answer, static_cast<size_t>(len)) : std::string(), err != nullptr ? err : "");
			}
			return {};
		});
	command("importForget", "the project will not take the imported movie's inputs", {}, [this](const Args &, std::string &) -> Value {
		pendingInput_.clear();
		imported.set(false);
		importNotes.set("");
		return {};
	});

	command("refresh", "lists the installed cores again", {}, [this](const Args &, std::string &) -> Value {
		std::string names;
		for (const auto &[name, path] : cores_.installed()) names += (names.empty() ? "" : "\n") + name;
		coreNames.set(names);
		return {};
	});
	command("chooseCore", "starts the project over with this installed core", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		chooseCore(name, error);
		return {};
	});
	command("chooseMachine", "the machine the project is, by its place in machines", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			const Json *list = cfg_.find("machines");
			if (list == nullptr || i < 0 || i >= static_cast<int64_t>(list->items.size()))
			{
				error = "no machine " + std::to_string(i);
				return {};
			}
			const Json &m = list->items[static_cast<size_t>(i)];
			const Json *when = m.find("when");
			const Json *id = m.find("id");
			const std::string value = when != nullptr && !when->items.empty() ? when->items[0].text : id != nullptr ? id->text : "";
			Json values = form_.values();
			setMember(values, form_.machineSetting(), Json::string(value));
			form_.setValues(values);
			machine.set(i);
			evaluate();
			return {};
		});
	command("addFile", "puts a file in a slot", {"slot", "path"}, [this](const Args &a, std::string &error) -> Value {
		std::string id, path;
		if (!arg(a, 0, id, error) || !arg(a, 1, path, error)) return {};
		const Slot *s = slot(id);
		if (s == nullptr)
		{
			error = core.get().empty() ? "choose a core first" : "the package has no slot " + id;
			return {};
		}
		if (!s->exposed)
		{
			error = s->title + " does not go with the files or the machine chosen";
			return {};
		}
		const std::string name = baseName(path), kind = kindOf(name);
		if (!s->formats.empty() && std::find(s->formats.begin(), s->formats.end(), kind) == s->formats.end())
		{
			error = name + ": " + s->title + " takes " + (s->formats.size() == 1 ? "a ." + s->formats[0] + " file" : "files of these kinds:");
			if (s->formats.size() > 1)
				for (const std::string &f : s->formats) error += " ." + (f.empty() ? std::string("(none)") : f);
			return {};
		}
		if (!s->namePattern.empty())
		{
			bool matches = false;
			try
			{
				matches = std::regex_match(name, std::regex(s->namePattern));
			}
			catch (const std::regex_error &)
			{
				matches = true; /* a pattern this cannot read is the core's to enforce */
			}
			if (!matches)
			{
				error = name + ": " + (!s->nameHelp.empty() ? s->nameHelp : s->help);
				return {};
			}
		}
		std::vector<Picked> &list = files_[id];
		if (s->max >= 0 && static_cast<int64_t>(list.size()) >= s->max)
		{
			error = s->title + " takes " + std::to_string(s->max) + (s->max == 1 ? " file" : " files") + " at most";
			return {};
		}
		std::error_code ec;
		if (!fs::is_regular_file(path, ec))
		{
			error = path + " is not a file";
			return {};
		}
		for (const auto &[other, picked] : files_)
			for (const Picked &f : picked)
				if (f.name == name)
				{
					error = "a file called " + name + " is already in the project";
					return {};
				}
		/* a cue's tracks are taken from beside it when the project is made:
		 * one that is not there is said now, not then */
		if (kind == "cue")
		{
			std::ifstream in(path, std::ios::binary);
			const std::string cue((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			uint64_t len = 0;
			const char *refs = ce_cue_references(cue.data(), cue.size(), &len);
			Json tracks;
			std::string jerr;
			if (refs != nullptr && json::parse(std::string_view(refs, static_cast<size_t>(len)), tracks, jerr))
				for (const Json &t : tracks.items)
					if (!fs::is_regular_file(fs::path(path).parent_path() / t.text, ec))
					{
						error = name + " names " + t.text + ", which is not beside it";
						return {};
					}
		}
		list.push_back({name, path});
		evaluate();
		return {};
	});
	command("removeFile", "takes a file out of a slot", {"slot", "index"}, [this](const Args &a, std::string &error) -> Value {
		std::string id;
		int64_t i = 0;
		if (!arg(a, 0, id, error) || !arg(a, 1, i, error)) return {};
		auto it = files_.find(id);
		if (it == files_.end() || i < 0 || i >= static_cast<int64_t>(it->second.size()))
		{
			error = "no file " + std::to_string(i) + " in " + id;
			return {};
		}
		it->second.erase(it->second.begin() + static_cast<long>(i));
		evaluate();
		return {};
	});
	command("moveFile", "moves a file within its slot (the order discs are swapped in)", {"slot", "from", "to"},
		[this](const Args &a, std::string &error) -> Value {
			std::string id;
			int64_t from = 0, to = 0;
			if (!arg(a, 0, id, error) || !arg(a, 1, from, error) || !arg(a, 2, to, error)) return {};
			auto it = files_.find(id);
			const int64_t n = it == files_.end() ? 0 : static_cast<int64_t>(it->second.size());
			if (from < 0 || from >= n || to < 0 || to >= n)
			{
				error = "no such place in " + id;
				return {};
			}
			Picked f = it->second[static_cast<size_t>(from)];
			it->second.erase(it->second.begin() + static_cast<long>(from));
			it->second.insert(it->second.begin() + static_cast<long>(to), f);
			evaluate();
			return {};
		});
	command("set", "changes a setting that applies", {"name", "value"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		if (a.size() < 2)
		{
			error = "argument 2 is missing";
			return {};
		}
		if (!form_.set(name, a[1], error)) return {};
		evaluate();
		return {};
	});
	command("chooseFirmware", "a file chosen by hand for a firmware the machine needs", {"id", "path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string id, path;
			if (!arg(a, 0, id, error) || !arg(a, 1, path, error)) return {};
			const auto need = std::find_if(needs_.begin(), needs_.end(), [&](const Need &n) { return n.id == id; });
			if (need == needs_.end())
			{
				error = "the machine needs no firmware " + id;
				return {};
			}
			char sha[41];
			uint64_t len = 0;
			if (ce_sha1_file(path.c_str(), sha, &len) != 1)
			{
				error = "cannot read " + path;
				return {};
			}
			chosenFirmware_[id] = {path, sha};
			evaluate();
			return {};
		});
	command("forgetFirmware", "a firmware chosen by hand goes back to the one found", {"id"},
		[this](const Args &a, std::string &error) -> Value {
			std::string id;
			if (!arg(a, 0, id, error)) return {};
			chosenFirmware_.erase(id);
			evaluate();
			return {};
		});
	command("create", "writes the project to this file and opens it (unless open is false)", {"path", "open"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			bool open = true;
			if (!arg(a, 0, path, error)) return {};
			if (a.size() > 1 && !arg(a, 1, open, error)) return {};
			if (!ready.get())
			{
				error = "not yet: " + problems.get();
				return {};
			}
			/* everything the project is, taken now: making it runs on the
			 * machine's thread, since it reads and hashes every file */
			std::string platform;
			if (const Json *list = cfg_.find("machines"); list != nullptr && machine.get() >= 0 && machine.get() < static_cast<int64_t>(list->items.size()))
			{
				if (const Json *id = list->items[static_cast<size_t>(machine.get())].find("id")) platform = id->text;
			}
			else if (const Json *system = cfg_.find("systemId"))
				platform = system->text;
			std::vector<std::pair<std::string, Picked>> files;
			for (const Slot &s : slots_)
				if (auto it = files_.find(s.id); it != files_.end())
					for (const Picked &f : it->second) files.emplace_back(s.id, f);
			std::string name = title.get();
			if (name.empty() && !files.empty()) name = fs::path(files.front().second.name).stem().string();
			Json pins;
			pins.kind = Json::Kind::Array;
			for (const Need &n : needs_)
			{
				Json pin;
				setMember(pin, "id", Json::string(n.id));
				setMember(pin, "sha1", Json::string(n.sha1));
				pins.items.push_back(pin);
			}
			const std::string settings = json::write(form_.recorded()), firmware = json::write(pins);
			const std::string coreName = core.get(), version = version_, sha = packageSha1_, slotsText = slotsText_, package = package_;
		const std::string input = pendingInput_;
			status.set("Making " + path + "...");
			session_.onMachine([=, this](ce_session *) {
				ce_project *p = ce_project_new();
				ce_project_set_core(p, coreName.c_str(), version.c_str(), sha.c_str());
				if (!platform.empty()) ce_project_header_set(p, "Platform", platform.c_str());
				const char *err = nullptr;
				std::string why;
				for (const auto &[slotId, f] : files)
					if (ce_project_file_add(p, f.name.c_str(), slotId.c_str(), f.path.c_str(), &err) != 0)
					{
						why = f.name + ": " + (err != nullptr ? err : "it could not be added");
						break;
					}
				if (why.empty() && ce_project_set_settings_text(p, settings.c_str(), &err) != 0) why = err != nullptr ? err : "settings";
				if (why.empty() && firmware != "[]" && ce_project_set_firmware_text(p, firmware.c_str(), &err) != 0) why = err != nullptr ? err : "firmware";
				ce_project_set_title(p, name.c_str());
			/* an imported movie's inputs are the project's */
			if (!input.empty()) ce_project_set_log_text(p, input.data(), input.size());
				if (why.empty() && !slotsText.empty() && ce_project_validate(p, slotsText.data(), slotsText.size(), &err) != 0)
					why = err != nullptr ? err : "the files do not fit the package's slots";
				if (why.empty() && ce_project_save(p, path.c_str(), &err) != 0) why = err != nullptr ? err : "it could not be written";
				ce_project_free(p);
				later([this, why, path, package, open] {
					if (!why.empty())
					{
						status.set("Not made: " + why);
						return;
					}
					status.set("Made " + path);
					std::string e;
					if (open) session_.run("openProject", {path, package}, e);
				});
			});
			return {};
		});

	auto listCores = [this] {
		std::string names;
		for (const auto &[name, path] : cores_.installed()) names += (names.empty() ? "" : "\n") + name;
		coreNames.set(names);
	};
	listCores();
	coresWatch_ = cores_.count.changed.connect(listCores);
}

bool NewProjectViewModel::chooseCore(const std::string &name, std::string &error)
{
	std::string package;
	for (const auto &[n, p] : cores_.installed())
		if (n == name) package = p;
	if (package.empty())
	{
		error = "no " + name + " core is installed";
		return false;
	}
	const char *perr = nullptr;
	ce_package *p = ce_package_open(package.c_str(), &perr);
	if (p == nullptr)
	{
		error = package + " cannot be read" + (perr != nullptr ? std::string(": ") + perr : "");
		return false;
	}
	const std::string config = entryText(p, "waterbox.config");
	slotsText_ = entryText(p, "file_slots.json");
	packageSha1_ = ce_package_sha1(p) != nullptr ? ce_package_sha1(p) : "";
	ce_package_free(p);
	std::string jerr;
	cfg_ = Json();
	if (!json::parse(config, cfg_, jerr))
	{
		error = package + ": its waterbox.config does not read (" + jerr + ")";
		return false;
	}
	package_ = package;
	version_ = cfg_.find("version") != nullptr ? cfg_.find("version")->text : "";

	/* the slots it declares; a package without the file is one game file of
	 * the kinds it says it takes */
	slots_.clear();
	atLeastOneOf_.clear();
	Json decl;
	if (!slotsText_.empty() && json::parse(slotsText_, decl, jerr))
	{
		if (const Json *list = decl.find("slots"))
			for (const Json &d : list->items)
			{
				Slot s;
				auto text = [&](const char *k) { const Json *j = d.find(k); return j != nullptr ? j->text : std::string(); };
				s.id = text("id");
				s.title = text("title");
				s.help = text("help");
				s.namePattern = text("namePattern");
				s.nameHelp = text("nameHelp");
				if (const Json *m = d.find("min")) s.min = static_cast<int64_t>(m->number());
				if (const Json *m = d.find("max")) s.max = static_cast<int64_t>(m->number());
				if (const Json *f = d.find("formats"))
					for (const Json &k : f->items) s.formats.push_back(lower(k.text));
				slots_.push_back(s);
			}
		if (const Json *groups = decl.find("atLeastOneOf"))
			for (const Json &g : groups->items)
			{
				std::vector<std::string> ids;
				for (const Json &id : g.items) ids.push_back(id.text);
				atLeastOneOf_.push_back(ids);
			}
	}
	else
	{
		slotsText_.clear();
		Slot s;
		s.id = "rom";
		s.title = "Game";
		s.min = 1;
		s.max = 1;
		if (const Json *ext = cfg_.find("extensions"))
			for (const auto &[e, system] : ext->members) s.formats.push_back(lower(e.size() > 1 && e[0] == '.' ? e.substr(1) : e));
		slots_.push_back(s);
	}

	/* the movie import it declares, if any */
	import_ = Json();
	importPicked_.clear();
	importOptions_.clear();
	pendingInput_.clear();
	imported.set(false);
	importNotes.set("");
	importMovie.set("");
	importMenu.set("");
	importMovieLabel.set("");
	importMovieFilter.set("");
	if (const Json *mi = cfg_.find("movieImport"); mi != nullptr && mi->kind == Json::Kind::Object)
	{
		import_ = *mi;
		if (const Json *m = mi->find("menu")) importMenu.set(m->text);
		if (const Json *mv = mi->find("movie"))
		{
			if (const Json *l = mv->find("label")) importMovieLabel.set(l->text);
			std::string filter;
			if (const Json *ex = mv->find("extensions"))
				for (const Json &e : ex->items) filter += (filter.empty() ? "" : " ") + std::string("*.") + lower(e.text[0] == '.' ? e.text.substr(1) : e.text);
			importMovieFilter.set(filter.empty() ? "*" : filter);
		}
		if (importMenu.get().empty()) importMenu.set("Import Movie...");
	}
	importFileRows_.reset.emit();
	importOptionRows_.reset.emit();

	/* its settings at their defaults, and its machines */
	std::vector<Json> settings;
	if (const Json *list = cfg_.find("settings")) settings = list->items;
	const std::string machineSetting = cfg_.find("machineSetting") != nullptr ? cfg_.find("machineSetting")->text : "";
	form_.declare(std::move(settings), machineSetting);
	Json values;
	values.kind = Json::Kind::Object;
	form_.setValues(values);
	std::string names;
	int64_t chosen = -1;
	if (const Json *list = cfg_.find("machines"); list != nullptr && !machineSetting.empty())
	{
		std::string preset;
		for (const Json &d : form_.declarations())
			if (const Json *n = d.find("name"); n != nullptr && n->text == machineSetting)
				if (const Json *v = d.find("default")) preset = v->text;
		for (size_t i = 0; i < list->items.size(); i++)
		{
			const Json &m = list->items[i];
			const Json *label = m.find("label");
			const Json *id = m.find("id");
			names += (i ? "\n" : "") + (label != nullptr ? label->text : id != nullptr ? id->text : std::to_string(i));
			if (const Json *when = m.find("when"); chosen < 0 && when != nullptr)
				for (const Json &w : when->items) chosen = w.text == preset ? static_cast<int64_t>(i) : chosen;
		}
		if (chosen < 0 && !list->items.empty()) chosen = 0;
	}
	machines.set(names);
	machine.set(chosen);
	files_.clear();
	chosenFirmware_.clear();
	firmwareIndex_ = indexFirmware(cores_.firmwareFolders());
	core.set(name);
	title.set("");
	status.set("A new " + name + " project");
	if (chosen >= 0)
	{
		std::string e;
		run("chooseMachine", {chosen}, e);
	}
	else
		evaluate();
	return true;
}

const Json *NewProjectViewModel::importInput(const std::string &option) const
{
	if (const Json *files = import_.find("files"))
		for (const Json &f : files->items)
			if (const Json *o = f.find("option"); o != nullptr && o->text == option) return &f;
	return nullptr;
}

void NewProjectViewModel::applyImport(const std::string &text)
{
	Json answer;
	std::string why;
	if (!json::parse(text, answer, why) || answer.kind != Json::Kind::Object)
	{
		status.set("Not imported: the core's answer does not read (" + why + ")");
		return;
	}
	if (const Json *refused = answer.find("error"))
	{
		status.set("Not imported: " + refused->text);
		return;
	}
	std::vector<std::string> notes;
	/* the settings it dictates; the machine's, by choosing the machine */
	if (const Json *settings = answer.find("settings"))
		for (const auto &[name, value] : settings->members)
		{
			if (name == form_.machineSetting())
			{
				if (const Json *list = cfg_.find("machines"))
					for (size_t i = 0; i < list->items.size(); i++)
						if (const Json *when = list->items[i].find("when"))
							for (const Json &w : when->items)
								if (w.text == value.text)
								{
									std::string e;
									run("chooseMachine", {static_cast<int64_t>(i)}, e);
								}
				continue;
			}
			Value v;
			switch (value.kind)
			{
			case Json::Kind::Bool: v = value.boolean; break;
			case Json::Kind::Number:
				v = std::floor(value.number()) == value.number() ? Value(static_cast<int64_t>(value.number())) : Value(value.number());
				break;
			default: v = value.text; break;
			}
			form_.evaluate();
			std::string e;
			if (!form_.set(name, v, e)) notes.push_back("Setting " + name + " not taken: " + e);
		}
	/* the files, in the answer's order: the ones picked for the import */
	files_.clear();
	if (const Json *files = answer.find("files"))
		for (const Json &f : files->items)
		{
			const Json *name = f.find("name"), *slot = f.find("slot");
			if (name == nullptr) continue;
			std::string path;
			for (const auto &[option, picked] : importPicked_)
				for (const std::string &p : picked)
					if (baseName(p) == name->text) path = p;
			if (path.empty())
			{
				notes.push_back(name->text + ": not among the files picked");
				continue;
			}
			std::string e;
			if (!run("addFile", {slot != nullptr ? slot->text : std::string("rom"), path}, e)) notes.push_back(name->text + ": " + e);
		}
	/* the firmware: a file picked for a firmware input is the one the answer names */
	if (const Json *firmware = answer.find("firmware"))
		for (const Json &need : firmware->items)
		{
			const Json *id = need.find("id"), *sha = need.find("sha1");
			if (id == nullptr) continue;
			if (const Json *files = import_.find("files"))
				for (const Json &input : files->items)
				{
					const Json *isFirmware = input.find("firmware"), *opt = input.find("option");
					if (isFirmware == nullptr || !isFirmware->boolean || opt == nullptr) continue;
					const auto it = importPicked_.find(opt->text);
					if (it == importPicked_.end()) continue;
					for (const std::string &p : it->second)
					{
						char got[41];
						uint64_t len = 0;
						if (ce_sha1_file(p.c_str(), got, &len) == 1 && (sha == nullptr || lower(got) == lower(sha->text)))
						{
							std::string e;
							run("chooseFirmware", {id->text, p}, e);
						}
					}
				}
		}
	if (const Json *n = answer.find("notes"))
		for (const Json &line : n->items) notes.push_back(line.text);
	pendingInput_ = answer.find("input") != nullptr ? answer.find("input")->text : std::string();
	imported.set(!pendingInput_.empty());
	std::string lines;
	for (const std::string &n : notes) lines += (lines.empty() ? "" : "\n") + n;
	importNotes.set(lines);
	if (title.get().empty()) title.set(fs::path(importMovie.get()).stem().string());
	evaluate();
	const Json *frames = answer.find("frames");
	status.set("Imported " + baseName(importMovie.get()) + (frames != nullptr ? " (" + frames->text + " frames)" : std::string()));
}

std::string NewProjectViewModel::slotMap() const
{
	Json map;
	map.kind = Json::Kind::Object;
	for (const Slot &s : slots_)
		if (auto it = files_.find(s.id); it != files_.end() && !it->second.empty())
		{
			Json names;
			names.kind = Json::Kind::Array;
			for (const Picked &f : it->second) names.items.push_back(Json::string(f.name));
			map.members.emplace_back(s.id, names);
		}
	return json::write(map);
}

const NewProjectViewModel::Slot *NewProjectViewModel::slot(const std::string &id) const
{
	for (const Slot &s : slots_)
		if (s.id == id) return &s;
	return nullptr;
}

void NewProjectViewModel::evaluate()
{
	const std::string map = slotMap();
	form_.setSlots(map);
	form_.evaluate();
	const Json recorded = form_.recorded();
	const std::string settingsText = json::write(recorded);

	/* the slots this map and these settings leave open: the engine's rule */
	if (!slotsText_.empty())
	{
		uint64_t len = 0;
		const char *out = ce_slots_evaluate(slotsText_.data(), slotsText_.size(), map.data(), map.size(),
			settingsText.data(), settingsText.size(), &len);
		Json ids;
		std::string jerr;
		const bool read = out != nullptr && json::parse(std::string_view(out, static_cast<size_t>(len)), ids, jerr);
		for (Slot &s : slots_)
		{
			s.exposed = !read;
			for (const Json &id : ids.items) s.exposed = s.exposed || id.text == s.id;
		}
	}

	/* the firmware these call for, found by the engine's rules unless chosen */
	needs_.clear();
	Json none, firmwareDecl, settingsDecl;
	none.kind = Json::Kind::Array;
	firmwareDecl = cfg_.find("firmware") != nullptr ? *cfg_.find("firmware") : none;
	settingsDecl.kind = Json::Kind::Array;
	settingsDecl.items = form_.declarations();
	for (const FirmwareNeed &n : neededFirmware(firmwareDecl, settingsDecl, map, recorded, none, firmwareIndex_))
	{
		Need need{n.id, n.display, n.path, n.how, n.sha1};
		if (auto it = chosenFirmware_.find(n.id); it != chosenFirmware_.end())
		{
			need.path = it->second.first;
			need.sha1 = it->second.second;
			need.how = "chosen";
		}
		needs_.push_back(need);
	}

	/* what is missing */
	std::string lines;
	auto say = [&lines](const std::string &line) { lines += (lines.empty() ? "" : "\n") + line; };
	if (core.get().empty()) say("Choose a core");
	for (const Slot &s : slots_)
	{
		const auto it = files_.find(s.id);
		const int64_t n = it == files_.end() ? 0 : static_cast<int64_t>(it->second.size());
		if (!s.exposed)
		{
			if (n > 0) say(s.title + " does not go with the files or the machine chosen: take its files out");
			continue;
		}
		if (n < s.min) say(s.title + ": " + (s.min == 1 ? std::string("a file is needed") : std::to_string(s.min) + " files are needed"));
		if (s.max >= 0 && n > s.max) say(s.title + ": " + std::to_string(s.max) + " at most");
	}
	for (const std::vector<std::string> &group : atLeastOneOf_)
	{
		int64_t n = 0;
		std::string titles;
		for (const std::string &id : group)
		{
			if (const auto it = files_.find(id); it != files_.end()) n += static_cast<int64_t>(it->second.size());
			if (const Slot *s = slot(id); s != nullptr && s->exposed) titles += (titles.empty() ? "" : ", ") + s->title;
		}
		if (n == 0 && !titles.empty()) say("One of these is needed: " + titles);
	}
	for (const Need &n : needs_)
		if (n.path.empty()) say(n.display + ": not found in the firmware folder; choose its file");
	problems.set(lines);
	ready.set(lines.empty());
	slotRows_.reset.emit();
	settingRows_.reset.emit();
	firmwareRows_.reset.emit();
}

std::string NewProjectViewModel::SlotRows::columnName(int32_t column) const
{
	static const char *const names[] = {"id", "title", "min", "max", "formats", "help", "exposed", "files", "count", "filter"};
	return column >= 0 && column < 10 ? names[column] : "";
}

Value NewProjectViewModel::SlotRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Slot &s = vm_.slots_[static_cast<size_t>(row)];
	const auto it = vm_.files_.find(s.id);
	switch (column)
	{
	case 0: return s.id;
	case 1: return s.title;
	case 2: return s.min;
	case 3: return s.max;
	case 4:
	{
		std::string out;
		for (const std::string &f : s.formats) out += (out.empty() ? "" : " ") + (f.empty() ? std::string("(no extension)") : "." + f);
		return out;
	}
	case 5: return s.help;
	case 6: return s.exposed;
	case 7:
	{
		std::string out;
		if (it != vm_.files_.end())
			for (const Picked &f : it->second) out += (out.empty() ? "" : "\n") + f.name;
		return out;
	}
	case 8: return it == vm_.files_.end() ? int64_t{0} : static_cast<int64_t>(it->second.size());
	case 9:
	{
		std::string out;
		for (const std::string &f : s.formats) out += (out.empty() ? "" : " ") + (f.empty() ? std::string("*") : "*." + f);
		return out.empty() ? std::string("*") : out;
	}
	default: return {};
	}
}

int64_t NewProjectViewModel::ImportFileRows::rowCount() const
{
	const Json *files = vm_.import_.find("files");
	return files != nullptr ? static_cast<int64_t>(files->items.size()) : 0;
}

std::string NewProjectViewModel::ImportFileRows::columnName(int32_t column) const
{
	static const char *const names[] = {"option", "label", "filter", "required", "multiple", "files"};
	return column >= 0 && column < 6 ? names[column] : "";
}

Value NewProjectViewModel::ImportFileRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Json &f = vm_.import_.find("files")->items[static_cast<size_t>(row)];
	auto text = [&](const char *k) { const Json *j = f.find(k); return j != nullptr ? j->text : std::string(); };
	auto flag = [&](const char *k) { const Json *j = f.find(k); return j != nullptr && j->boolean; };
	switch (column)
	{
	case 0: return text("option");
	case 1: return text("label");
	case 2:
	{
		std::string filter;
		if (const Json *ex = f.find("extensions"))
			for (const Json &e : ex->items) filter += (filter.empty() ? "" : " ") + std::string("*.") + lower(!e.text.empty() && e.text[0] == '.' ? e.text.substr(1) : e.text);
		return filter.empty() ? std::string("*") : filter;
	}
	case 3: return flag("required");
	case 4: return flag("multiple");
	case 5:
	{
		std::string names;
		const auto it = vm_.importPicked_.find(text("option"));
		if (it != vm_.importPicked_.end())
			for (const std::string &p : it->second) names += (names.empty() ? "" : "\n") + baseName(p);
		return names;
	}
	default: return {};
	}
}

int64_t NewProjectViewModel::ImportOptionRows::rowCount() const
{
	const Json *opts = vm_.import_.find("options");
	return opts != nullptr ? static_cast<int64_t>(opts->items.size()) : 0;
}

std::string NewProjectViewModel::ImportOptionRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "label", "on"};
	return column >= 0 && column < 3 ? names[column] : "";
}

Value NewProjectViewModel::ImportOptionRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Json &o = vm_.import_.find("options")->items[static_cast<size_t>(row)];
	const Json *name = o.find("name"), *label = o.find("display");
	const std::string n = name != nullptr ? name->text : "";
	switch (column)
	{
	case 0: return n;
	case 1: return label != nullptr ? label->text : n;
	case 2:
	{
		const auto it = vm_.importOptions_.find(n);
		return it != vm_.importOptions_.end() && it->second;
	}
	default: return {};
	}
}

std::string NewProjectViewModel::FirmwareRows::columnName(int32_t column) const
{
	static const char *const names[] = {"id", "name", "file", "how", "missing"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value NewProjectViewModel::FirmwareRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Need &n = vm_.needs_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return n.id;
	case 1: return n.display;
	case 2: return n.path;
	case 3: return n.how;
	case 4: return n.path.empty();
	default: return {};
	}
}

} // namespace chimera::vm
