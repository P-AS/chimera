#pragma once
/* A game core's properties by name, for Lua's game library as Chimera has
 * it (docs/game-cores.md): the names in the core's order, a value read or
 * set by the engine's rules, and what each is. An emulator core has none. */

#include <string>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class GamePropertiesViewModel : public ViewModel
{
public:
	GamePropertiesViewModel(Dispatcher &dispatcher, SessionViewModel &session);
	/* why the last get or set did not happen */
	Property<std::string> lastError;

private:
	SessionViewModel &session_;
};

} // namespace chimera::vm
