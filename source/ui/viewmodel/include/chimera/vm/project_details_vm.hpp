#pragma once
/* The open project's details, as Chimera's TAStudio edits them: its header
 * (author, emulator version, board name, game name; the core is shown, not
 * edited, since the project's core is what its package pin says), its
 * comments, and its subtitles - a line of text shown over the picture from a
 * frame, for a number of frames, at a place and in a colour. Changed here,
 * they are saved with the project. The subtitles of the frame on screen are
 * worked out for the window to draw. */

#include <string>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class ProjectDetailsViewModel : public ViewModel
{
public:
	ProjectDetailsViewModel(Dispatcher &dispatcher, SessionViewModel &session);

	Property<std::string> author, emulatorVersion, boardName, gameName, core;
	/* the comments, a line each */
	Property<std::string> comments;
	/* the author a new project starts with (Chimera's "default user"), remembered */
	Property<std::string> defaultAuthor{"default user"};
	Property<bool> showSubtitles{true};
	/* the subtitles on the frame shown, as [{"text", "x", "y", "color"}] */
	Property<std::string> onScreen{"[]"};
	Property<int64_t> count{0};
	Property<std::string> status;

	struct Subtitle
	{
		int64_t frame = 0, x = 0, y = 0, duration = 120;
		/* ARGB, as Chimera keeps it */
		uint32_t color = 0xFFFFFFFFu;
		std::string message;
	};
	const std::vector<Subtitle> &subtitles() const { return subtitles_; }

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(ProjectDetailsViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.subtitles_.size()); }
		int32_t columnCount() const override { return 6; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		ProjectDetailsViewModel &vm_;
	};

	/* read from the open project, and written back to it */
	void load();
	void storeSubtitles();
	void showFrame();
	bool subtitleArgs(const Args &a, size_t from, Subtitle &out, std::string &error) const;

	SessionViewModel &session_;
	std::vector<Subtitle> subtitles_;
	Rows rows_{*this};
	Connection opensWatch_, projectWatch_, frameWatch_, showWatch_;
};

} // namespace chimera::vm
