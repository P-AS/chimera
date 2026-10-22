#pragma once
/* What the window remembers between runs: any view model's property, kept by
 * its name in the registry ("cores.coresFolder", "session.autoRestore"),
 * restored when the store takes it and written whenever it changes. Nothing
 * here knows any property: the registry says what each is. The file is the
 * window's own (never Chimera's), plain JSON. */

#include <string>
#include <vector>

#include "chimera/vm/json.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class ConfigStore
{
public:
	/* reads the file when there is one; a file that will not read is set
	 * aside as it is (renamed .unreadable) rather than written over */
	explicit ConfigStore(std::string path);

	/* restores the property from the file, then keeps it there */
	void keep(ViewModel &vm, const std::string &property);

	const std::string &path() const { return path_; }
	/* "" when all is well, else what went wrong reading or writing */
	const std::string &problem() const { return problem_; }

private:
	void save();

	std::string path_, problem_;
	json::Json root_;
	std::vector<Connection> watches_;
};

} // namespace chimera::vm
