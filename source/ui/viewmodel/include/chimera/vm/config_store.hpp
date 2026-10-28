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
	/* Chimera's Save Config As: everything kept, written to another file */
	bool saveAs(const std::string &path, std::string &error);
	/* Chimera's Load Config From: what that file says of each property kept,
	 * set (and so kept here too); its other entries are left. How many were
	 * set, or -1 with the reason */
	int64_t loadFrom(const std::string &path, std::string &error);
	/* "" when all is well, else what went wrong reading or writing */
	const std::string &problem() const { return problem_; }
	/* a switch the file keeps ("customize.singleInstance"), read before
	 * anything is made (what the program does at its start); `otherwise`
	 * when the file, or the switch in it, is missing */
	static bool switchIn(const std::string &path, const std::string &key, bool otherwise);
	/* a text the file keeps ("customize.language"), read the same way */
	static std::string textIn(const std::string &path, const std::string &key, const std::string &otherwise);

private:
	void save();

	std::string path_, problem_;
	json::Json root_;
	struct Kept
	{
		ViewModel *vm;
		std::string property, key;
	};
	std::vector<Kept> kept_;
	std::vector<Connection> watches_;
};

} // namespace chimera::vm
