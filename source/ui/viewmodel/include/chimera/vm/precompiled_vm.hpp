#pragma once
/* Chimera's Pre-Compiled Modules window (Tools): the games a core has
 * translated code for, a folder each named by the game's SHA1, and the only
 * place that code is taken away. A row is a folder, manifest or not: a
 * compile cut short leaves code and no manifest, and is the row most worth
 * removing. Named, dated and counted by its manifest.json when there is
 * one (Chimera's: rom, core, coreVersion, compiled, files). The open
 * machine's folder is in use and stays. */

#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class PrecompiledViewModel : public ViewModel
{
public:
	explicit PrecompiledViewModel(Dispatcher &dispatcher);

	Property<std::string> root, inUse, total, status;
	/* the folder a compile is filling: it stays, as the open machine's does */
	Property<std::string> compiling;
	/* Chimera's "Memory per compile session (GB)": how much memory one compile
	 * session is counted at, 1 to 256, 8 at first (remembered) */
	Property<int64_t> sessionGigabytes{8};
	/* what Chimera's window says over the list */
	Property<std::string> header;
	/* a size as Chimera's Pre-Compiled Modules writes one ("1.25 GB", "12.0 MB", "40 KB") */
	static std::string sizeText(uint64_t bytes);

	struct Game
	{
		std::string sha1, name, core, version, compiled, folder;
		uint64_t bytes = 0;
		int64_t modules = 0;
		bool complete = false, unknown = true, open = false;
	};
	const std::vector<Game> &games() const { return games_; }

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(PrecompiledViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.games_.size()); }
		int32_t columnCount() const override { return 15; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PrecompiledViewModel &vm_;
	};
	void survey();
	std::vector<Game> games_;
	Rows rows_{*this};
	Connection rootWatch_, inUseWatch_;
};

} // namespace chimera::vm
