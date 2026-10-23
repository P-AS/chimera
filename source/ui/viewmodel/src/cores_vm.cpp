#include "chimera/vm/cores_vm.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#include "chimera/engine.h"
#include "chimera/vm/european_clock.hpp"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

CoresViewModel::CoresViewModel(Dispatcher &dispatcher) : ViewModel("cores", dispatcher)
{
	expose("coresFolder", "the folder core packages are looked for in", coresFolder, true);
	expose("firmwareFolder", "the folder firmware is looked for in", firmwareFolder, true);
	expose("chosenFirmware", "the firmware files chosen by hand, every core's, as {\"core\": {\"id\": \"file\"}}", chosenFirmware, true);
	expose("count", "packages found", count);
	expose("status", "what the last look at the folder found", status);
	rows("cores", "one row a package: core, version, when it was made, file", rows_);
	command("refresh", "looks at the cores folder again", {}, [this](const Args &, std::string &) -> Value {
		scan();
		return count.get();
	});
	/* the environment names them where a test or a portable install means it */
	if (const char *env = std::getenv("CHIMERA_CORES_DIR")) coresFolder.set(env);
	if (const char *env = std::getenv("CHIMERA_FIRMWARE_DIR")) firmwareFolder.set(env);
	folderWatch_ = coresFolder.changed.connect([this] { scan(); });
	firmwareWatch_ = firmwareFolder.changed.connect([this] {
		std::lock_guard<std::mutex> lock(m_);
		firmwareM_ = firmwareFolder.get();
	});
	firmwareM_ = firmwareFolder.get();
	chosenWatch_ = chosenFirmware.changed.connect([this] {
		json::Json all;
		std::string error;
		std::vector<std::string> files;
		if (json::parse(chosenFirmware.get(), all, error))
			for (const auto &[core, ids] : all.members)
				for (const auto &[id, file] : ids.members)
					if (file.kind == json::Json::Kind::String && !file.text.empty()) files.push_back(file.text);
		std::lock_guard<std::mutex> lock(m_);
		chosenM_ = std::move(files);
	});
	scan();
}

void CoresViewModel::rememberFirmware(const std::string &coreName, const std::string &id, const std::string &path)
{
	json::Json all;
	std::string error;
	if (!json::parse(chosenFirmware.get(), all, error) || all.kind != json::Json::Kind::Object) all = json::Json(), all.kind = json::Json::Kind::Object;
	auto member = [](json::Json &obj, const std::string &name) -> json::Json & {
		for (auto &[k, v] : obj.members)
			if (k == name) return v;
		obj.members.emplace_back(name, json::Json());
		return obj.members.back().second;
	};
	json::Json &mine = member(all, coreName);
	if (mine.kind != json::Json::Kind::Object) mine = json::Json(), mine.kind = json::Json::Kind::Object;
	member(mine, id) = json::Json::string(path);
	chosenFirmware.set(json::write(all));
}

void CoresViewModel::scan()
{
	namespace fs = std::filesystem;
	std::vector<Core> found;
	const std::string folder = coresFolder.get();
	std::error_code ec;
	if (!folder.empty() && fs::is_directory(folder, ec))
		for (const auto &entry : fs::directory_iterator(folder, ec))
		{
			if (entry.path().extension() != ".chimeraCore") continue;
			const std::string path = entry.path().string();
			const char *perr = nullptr;
			ce_package *p = ce_package_open(path.c_str(), &perr);
			if (p == nullptr) continue;
			uint64_t len = 0;
			const uint8_t *d = ce_package_entry(p, "waterbox.config", &len);
			json::Json cfg;
			std::string error;
			if (d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, error))
			{
				Core c;
				if (const json::Json *n = cfg.find("coreName")) c.coreName = n->text;
				if (const json::Json *v = cfg.find("version")) c.version = v->text;
				if (const json::Json *m = cfg.find("versionDate")) c.made = m->text;
				c.dated = !c.made.empty() && parseIsoTime(c.made, c.date);
				if (const json::Json *k = cfg.find("kind"))
				{
					std::string kind = k->text;
					for (char &ch : kind) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					c.gameCore = kind == "game";
				}
				c.systems = systemsOf(cfg);
				c.path = path;
				c.modified = static_cast<long long>(fs::last_write_time(entry.path(), ec).time_since_epoch().count());
				/* the files it takes, as Chimera reads them: the package's
				 * "extensions" and then each machine's */
				const auto take = [&c](const json::Json *exts) {
					if (exts == nullptr) return;
					for (const auto &[ext, system] : exts->members)
					{
						std::string e = ext;
						for (char &ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
						if (!e.empty() && e[0] == '.') e.erase(0, 1);
						if (!e.empty() && std::find(c.extensions.begin(), c.extensions.end(), e) == c.extensions.end()) c.extensions.push_back(e);
					}
				};
				take(cfg.find("extensions"));
				if (const json::Json *machines = cfg.find("machines"))
					for (const json::Json &m : machines->items) take(m.find("extensions"));
				if (!c.coreName.empty()) found.push_back(std::move(c));
			}
			ce_package_free(p);
		}
	/* by core, then newest first by the date the package says it was made
	 * (one that says none after those that do), as Chimera orders them */
	auto folded = [](std::string s) {
		for (char &ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return s;
	};
	std::sort(found.begin(), found.end(), [&](const Core &a, const Core &b) {
		const std::string fa = folded(a.coreName), fb = folded(b.coreName);
		if (fa != fb) return fa < fb;
		if (a.coreName != b.coreName) return a.coreName < b.coreName;
		if (a.dated != b.dated) return a.dated;
		if (a.dated && a.date != b.date) return a.date > b.date;
		return a.modified > b.modified;
	});
	/* each one's line; the package's hash only where two are of one core */
	for (size_t i = 0; i < found.size(); i++)
	{
		Core &c = found[i];
		const bool shared = (i > 0 && found[i - 1].coreName == c.coreName) || (i + 1 < found.size() && found[i + 1].coreName == c.coreName);
		std::string version = c.version;
		const bool local = version.find("+local") != std::string::npos;
		if (const size_t plus = version.find('+'); plus != std::string::npos) version.erase(plus);
		const bool dirty = version.size() >= 6 && version.compare(version.size() - 6, 6, "-dirty") == 0;
		if (dirty) version.erase(version.size() - 6);
		if (version.size() > 8) version.erase(8);
		if (!version.empty() && (local || dirty)) version += " local";
		std::string dated = c.dated ? europeanTime(c.date) : std::string();
		if (!dated.empty() && !version.empty()) dated += "  (" + version + ")";
		else if (dated.empty()) dated = version;
		std::string build;
		if (shared)
		{
			const char *perr = nullptr;
			if (ce_package *p = ce_package_open(c.path.c_str(), &perr))
			{
				const char *sha = ce_package_sha1(p);
				if (sha != nullptr && std::string(sha).size() >= 8) build = "  [package " + std::string(sha).substr(0, 8) + "]";
				ce_package_free(p);
			}
		}
		c.line = c.coreName + (dated.empty() ? "" : "  " + dated) + build + "  -  " + c.systems;
	}
	const int64_t n = static_cast<int64_t>(found.size());
	{
		std::lock_guard<std::mutex> lock(m_);
		cores_ = std::move(found);
	}
	count.set(n);
	status.set(folder.empty() ? "No cores folder" : std::to_string(n) + (n == 1 ? " package in " : " packages in ") + folder);
	rows_.reset.emit();
}

std::string CoresViewModel::packageFor(const std::string &coreName, const std::string &pinnedSha1, std::string &note) const
{
	std::vector<Core> builds;
	{
		std::lock_guard<std::mutex> lock(m_);
		for (const Core &c : cores_)
			if (c.coreName == coreName) builds.push_back(c);
	}
	if (builds.empty()) return {};
	/* the pinned one, when it is here: only then is its hash worth computing */
	if (!pinnedSha1.empty())
		for (const Core &c : builds)
		{
			const char *perr = nullptr;
			ce_package *p = ce_package_open(c.path.c_str(), &perr);
			if (p == nullptr) continue;
			const char *sha = ce_package_sha1(p);
			const bool match = sha != nullptr && pinnedSha1 == sha;
			ce_package_free(p);
			if (match) return c.path;
		}
	/* else the most recently installed (they are sorted newest first) */
	if (!pinnedSha1.empty()) note = "the package the project was made with is not here; the newest " + coreName + " is";
	return builds.front().path;
}

std::vector<std::pair<std::string, std::string>> CoresViewModel::coresFor(const std::string &extension) const
{
	std::vector<std::pair<std::string, std::string>> out;
	std::lock_guard<std::mutex> lock(m_);
	/* sorted by core, newest first: the first of each core is the one */
	for (const Core &c : cores_)
	{
		if (!out.empty() && out.back().first == c.coreName) continue;
		if (std::find(c.extensions.begin(), c.extensions.end(), extension) != c.extensions.end())
			out.emplace_back(c.coreName, c.path);
	}
	return out;
}

std::vector<std::pair<std::string, std::string>> CoresViewModel::installed() const
{
	std::vector<std::pair<std::string, std::string>> out;
	std::lock_guard<std::mutex> lock(m_);
	for (const Core &c : cores_)
		if (out.empty() || out.back().first != c.coreName) out.emplace_back(c.coreName, c.path);
	return out;
}

std::string CoresViewModel::systemsOf(const json::Json &cfg)
{
	std::vector<std::string> ids;
	if (const json::Json *machines = cfg.find("machines"))
		for (const json::Json &m : machines->items)
			if (const json::Json *id = m.find("id"); id != nullptr && !id->text.empty() && std::find(ids.begin(), ids.end(), id->text) == ids.end())
				ids.push_back(id->text);
	if (ids.empty())
		if (const json::Json *one = cfg.find("systemId"); one != nullptr && !one->text.empty()) ids.push_back(one->text);
	const json::Json *names = cfg.find("systemNames");
	std::string out;
	for (const std::string &id : ids)
	{
		const json::Json *word = names != nullptr ? names->find(id) : nullptr;
		const bool said = word != nullptr && word->text.find_first_not_of(" \t") != std::string::npos;
		out += (out.empty() ? "" : ", ") + (said ? word->text : id);
	}
	return out;
}

std::vector<CoresViewModel::Package> CoresViewModel::packages() const
{
	std::vector<Package> out;
	std::lock_guard<std::mutex> lock(m_);
	for (const Core &c : cores_) out.push_back({c.coreName, c.line, c.path, c.gameCore});
	return out;
}

std::string CoresViewModel::chosenFirmwareFor(const std::string &coreName, const std::string &id) const
{
	json::Json all;
	std::string error;
	if (!json::parse(chosenFirmware.get(), all, error)) return {};
	const json::Json *mine = all.find(coreName);
	const json::Json *file = mine != nullptr ? mine->find(id) : nullptr;
	return file != nullptr && file->kind == json::Json::Kind::String ? file->text : std::string();
}

std::vector<std::string> CoresViewModel::firmwareFolders() const
{
	std::lock_guard<std::mutex> lock(m_);
	std::vector<std::string> out;
	if (!firmwareM_.empty()) out.push_back(firmwareM_);
	out.insert(out.end(), chosenM_.begin(), chosenM_.end());
	return out;
}

int64_t CoresViewModel::Rows::rowCount() const
{
	std::lock_guard<std::mutex> lock(vm_.m_);
	return static_cast<int64_t>(vm_.cores_.size());
}

std::string CoresViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"core", "version", "made", "takes", "file"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value CoresViewModel::Rows::cell(int64_t row, int32_t column) const
{
	std::lock_guard<std::mutex> lock(vm_.m_);
	if (row < 0 || row >= static_cast<int64_t>(vm_.cores_.size())) return {};
	const Core &c = vm_.cores_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return c.coreName;
	case 1: return c.version.substr(0, 12);
	case 2: return c.dated ? europeanTime(c.date) : c.made;
	case 3:
	{
		std::string takes;
		for (const std::string &e : c.extensions) takes += (takes.empty() ? "." : " .") + e;
		return takes;
	}
	case 4: return std::filesystem::path(c.path).filename().string();
	default: return {};
	}
}

} // namespace chimera::vm
