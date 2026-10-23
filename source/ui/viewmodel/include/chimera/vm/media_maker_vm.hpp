#pragma once
/* Chimera's Reproducible Media Maker (Tools): a folder turned into ONE file,
 * the same file on anybody's machine - a zip stored, an ISO 9660 disc or a
 * FAT12 floppy - so that a game dumped as a folder can be named in a project
 * by its SHA-1. The packing is the engine's (ce_media_make); an installed
 * core's recipe for its discs is handed over when the folder is one it was
 * written for. Here the folder, the shape, the name, how far it has got, and
 * stopping it. */

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class MediaMakerViewModel : public ViewModel
{
public:
	/* one installed core's recipe ("media" in its package): the core, what
	 * it calls such a folder, the recipe as the package wrote it */
	struct Recipe
	{
		std::string core, label, json;
	};
	using RecipeFinder = std::function<std::vector<Recipe>()>;

	MediaMakerViewModel(Dispatcher &dispatcher, RecipeFinder recipes);
	~MediaMakerViewModel() override;

	Property<std::string> folder, output;
	/* 0 zip stored, 1 ISO 9660 + Joliet, 2 FAT12 floppy, as the engine numbers them */
	Property<int64_t> format{0};
	Property<std::string> formatHelp;
	/* what an installed core's recipe recognised the folder as, "" for none */
	Property<std::string> recognised;
	Property<bool> running{false};
	/* the file being written ("NAME  (3 of 9)"), how far (0 to 1), and the outcome */
	Property<std::string> current, result;
	Property<double> progress{0.0};
	Property<std::string> sha1;

private:
	void recognise();
	RecipeFinder recipes_;
	std::string recipeJson_;
	std::thread worker_;
	std::atomic<bool> cancel_{false};
	Connection formatWatch_;
};

} // namespace chimera::vm
