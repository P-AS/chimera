#include "chimera/vm/config_files_vm.hpp"

namespace chimera::vm {

ConfigFilesViewModel::ConfigFilesViewModel(Dispatcher &dispatcher, ConfigStore &store) : ViewModel("configFiles", dispatcher), store_(store)
{
	expose("status", "what the config menu last did", status);
	command("save", "Chimera's Save Config: the settings are written as they change, so this says where", {},
		[this](const Args &, std::string &error) -> Value {
			if (!store_.problem().empty())
			{
				error = store_.problem();
				return {};
			}
			status.set("Settings saved in " + store_.path());
			return store_.path();
		});
	command("saveAs", "every setting kept, written to this file", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string path;
		if (!arg(a, 0, path, error)) return {};
		if (!store_.saveAs(path, error)) return {};
		status.set("Settings saved as " + path);
		return {};
	});
	command("load", "the settings file read again", {}, [this](const Args &, std::string &error) -> Value {
		const int64_t n = store_.loadFrom(store_.path(), error);
		if (n < 0) return {};
		status.set("Settings loaded from " + store_.path());
		return n;
	});
	command("loadFrom", "the settings in this file, set; kept from now on as this window's own", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			const int64_t n = store_.loadFrom(path, error);
			if (n < 0) return {};
			status.set("Settings loaded from " + path + " (" + std::to_string(n) + ")");
			return n;
		});
}

} // namespace chimera::vm
