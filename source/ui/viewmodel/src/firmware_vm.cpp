#include "chimera/vm/firmware_vm.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "chimera/engine.h"
#include "chimera/vm/firmware.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string field(const json::Json &j, const char *name)
{
	const json::Json *v = j.find(name);
	return v != nullptr ? v->text : std::string();
}

const char *const kStateText[] = {"on hand", "not a dump the core knows", "chosen, but gone", "missing"};
} // namespace

FirmwareViewModel::FirmwareViewModel(Dispatcher &dispatcher, CoresViewModel &cores) : ViewModel("firmware", dispatcher), cores_(cores)
{
	expose("status", "what the firmware window last had to say", status);
	expose("missing", "rows with nothing on hand", missing);
	rows("firmware", "one row a declared firmware file of an installed core: core, id, name, state (0 on hand and right, 1 "
		"on hand but not a dump the core knows, 2 chosen but gone, 3 nothing), stateText, path, how", model_);
	command("refresh", "asks every installed core again what firmware it declares, and looks for each", {},
		[this](const Args &, std::string &) -> Value {
			survey();
			return static_cast<int64_t>(rows_.size());
		});
	command("setFile", "a file of the person's for a row (remembered where it is, not copied); its hash decides whether it "
		"is a dump the core knows", {"row", "path"}, [this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			std::string path;
			if (!rowOf(a, i, error) || !arg(a, 1, path, error)) return {};
			if (!fs::is_regular_file(path))
			{
				error = path + " is not a file";
				return {};
			}
			const Row r = rows_[i];
			cores_.rememberFirmware(r.core, r.id, path);
			survey();
			for (const Row &now : rows_)
				if (now.core == r.core && now.id == r.id) status.set(fs::path(path).filename().string() + ": " + kStateText[now.state]);
			return {};
		});
	command("clearFile", "forgets the file chosen for a row; it is looked for in the firmware folder again", {"row"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			if (!rowOf(a, i, error)) return {};
			cores_.rememberFirmware(rows_[i].core, rows_[i].id, "");
			survey();
			return {};
		});
	command("scanFolder", "hashes the files in a folder (and below it when asked) and remembers each that answers a row "
		"with nothing usable on hand (gone or missing); says how many", {"folder", "subfolders"}, [this](const Args &a, std::string &error) -> Value {
			std::string folder;
			bool recurse = true;
			if (!arg(a, 0, folder, error)) return {};
			if (a.size() > 1 && !arg(a, 1, recurse, error)) return {};
			if (!fs::is_directory(folder))
			{
				error = folder + " is not a folder";
				return {};
			}
			const std::vector<FirmwareFile> index = indexFirmware({folder}, recurse, 10000);
			int64_t found = 0;
			for (const Row &r : std::vector<Row>(rows_))
			{
				/* only what has nothing usable: a file the person chose, their own dump, stays */
				if (r.state < 2) continue;
				json::Json decl;
				std::string err;
				json::parse(r.declJson, decl, err);
				std::string how;
				if (const FirmwareFile *f = findFirmware(decl, "", index, how))
				{
					cores_.rememberFirmware(r.core, r.id, f->path);
					found++;
				}
			}
			survey();
			status.set("Found " + std::to_string(found) + (found == 1 ? " firmware file" : " firmware files") + " in " + folder);
			return found;
		});
}

bool FirmwareViewModel::rowOf(const Args &a, size_t &i, std::string &error) const
{
	int64_t row = 0;
	if (!arg(a, 0, row, error)) return false;
	if (row < 0 || row >= static_cast<int64_t>(rows_.size()))
	{
		error = "no row " + std::to_string(row);
		return false;
	}
	i = static_cast<size_t>(row);
	return true;
}

void FirmwareViewModel::survey()
{
	rows_.clear();
	/* the firmware folder's files, and those chosen by hand: names and sizes, read only when a hash is asked */
	const std::vector<FirmwareFile> index = indexFirmware(cores_.firmwareFolders());
	for (const auto &[core, package] : cores_.installed())
	{
		const char *perr = nullptr;
		ce_package *pk = ce_package_open(package.c_str(), &perr);
		if (pk == nullptr) continue;
		uint64_t len = 0;
		json::Json cfg;
		std::string err;
		if (const uint8_t *d = ce_package_entry(pk, "waterbox.config", &len);
			d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, err))
			if (const json::Json *list = cfg.find("firmware"); list != nullptr && list->kind == json::Json::Kind::Array)
				for (const json::Json &decl : list->items)
				{
					Row r;
					r.core = core;
					r.id = field(decl, "id");
					r.name = field(decl, "display").empty() ? (field(decl, "name").empty() ? r.id : field(decl, "name")) : field(decl, "display");
					r.declJson = json::write(decl);
					const std::string sha = lower(field(decl, "sha1"));
					const std::string chosen = cores_.chosenFirmwareFor(core, r.id);
					if (!chosen.empty())
					{
						r.path = chosen;
						if (!fs::is_regular_file(chosen))
							r.state = 2;
						else
						{
							char hash[41] = {};
							uint64_t size = 0;
							r.state = sha.empty() || (ce_sha1_file(chosen.c_str(), hash, &size) == 1 && lower(hash) == sha) ? 0 : 1;
							r.how = "chosen";
						}
					}
					else if (const FirmwareFile *f = findFirmware(decl, "", index, r.how))
					{
						r.path = f->path;
						r.state = 0;
					}
					rows_.push_back(std::move(r));
				}
		ce_package_free(pk);
	}
	int64_t none = 0;
	for (const Row &r : rows_) none += r.state != 0 ? 1 : 0;
	missing.set(none);
	model_.reset.emit();
}

std::string FirmwareViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"core", "id", "name", "state", "stateText", "path", "how"};
	return column >= 0 && column < 7 ? names[column] : "";
}

Value FirmwareViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Row &r = vm_.rows_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return r.core;
	case 1: return r.id;
	case 2: return r.name;
	case 3: return static_cast<int64_t>(r.state);
	case 4: return std::string(kStateText[r.state]);
	case 5: return r.path;
	case 6: return r.how;
	default: return {};
	}
}

} // namespace chimera::vm
