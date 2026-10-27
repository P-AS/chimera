#pragma once
/* Lua's comm library, as Chimera has it: data shared with other programs on
 * this machine through named memory-mapped files, and nothing else (Chimera
 * has no network code). A name is a Windows file mapping's name; on Linux it
 * is the shared memory /dev/shm/<name>. A mapping made here is kept open
 * while the program runs, as Chimera's are. */

#include <functional>
#include <map>
#include <string>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class CommViewModel : public ViewModel
{
public:
	/* screenshot: the picture on screen as PNG bytes ("" for none) */
	CommViewModel(Dispatcher &dispatcher, std::function<std::string()> screenshot);
	~CommViewModel() override;

	/* where mmfScreenshot writes (Chimera's --mmf) */
	Property<std::string> screenshotFile;

	/* bytes written into the named mapping (made or grown to hold them); how many, -1 with why */
	int64_t write(const std::string &name, const std::string &bytes, std::string &error);
	/* size bytes read from the named mapping, which must be there */
	bool read(const std::string &name, int64_t size, std::string &out, std::string &error);

private:
	struct Mapping;
	std::function<std::string()> screenshot_;
	std::map<std::string, Mapping *> open_;
};

} // namespace chimera::vm
