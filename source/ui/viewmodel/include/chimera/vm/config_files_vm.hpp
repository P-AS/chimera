#pragma once
/* Chimera's Save Config, Save Config As, Load Config and Load Config From
 * (Config menu), over the window's settings file. The prototype writes a
 * setting the moment it changes, so Save Config says where it is; Load
 * Config reads that file again; the other two use a file of the person's. */

#include "chimera/vm/config_store.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class ConfigFilesViewModel : public ViewModel
{
public:
	ConfigFilesViewModel(Dispatcher &dispatcher, ConfigStore &store);
	Property<std::string> status;

private:
	ConfigStore &store_;
};

} // namespace chimera::vm
