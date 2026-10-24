#pragma once
/* Chimera's File > Encode Video: a video is an encode of a run, made in one
 * act. It asks for everything at once - the file, the ffmpeg command (or a
 * preset's), the two markers to encode between (both included), the size
 * (the machine's, or another, padded or stretched to it) and whether to
 * sync to the sound - then winds the run to the first marker, plays it as
 * fast as the machine goes, read only and keeping no state, pipes every
 * frame and its sound to ffmpeg as a NUT stream, closes the file and puts
 * the machine back on the frame it was on. A picture whose size changes
 * starts a new file (name_1.mp4), as Chimera's does. */

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class EncoderViewModel : public ViewModel
{
public:
	/* appDir: the program's folder, where the bundle keeps ffmpeg */
	EncoderViewModel(Dispatcher &dispatcher, SessionViewModel &session, std::string appDir);
	~EncoderViewModel() override;

	static const char *const kDefaultCommand;
	struct Preset
	{
		const char *name, *command;
	};
	static const std::vector<Preset> &presets();
	/* the container the command names (-f), as the file's extension; "" for none */
	static std::string extensionOf(const std::string &command);
	/* how far an encode is, as Chimera's window says it:
	 * "frame 1,234 / 5,000   60 fps   about 1m 4s remaining" (no time left
	 * said until a speed is known) */
	static std::string progressText(int64_t done, int64_t total, double framesPerSecond);
	/* the ffmpeg the encode runs: CHIMERA_FFMPEG, else beside the program,
	 * else the build's; "" when there is none */
	std::string ffmpegPath() const;

	Property<std::string> output, commandLine{kDefaultCommand}, preset;
	Property<int64_t> fromFrame{0}, toFrame{0};
	Property<bool> resize{false}, pad{false}, audioSync{true};
	Property<int64_t> width{0}, height{0};
	/* Chimera's Capture OSD and Capture Lua: what is drawn over the picture
	 * is in the video too - the counters and messages with what scripts draw
	 * (OSD), or what scripts draw alone (Lua) */
	Property<bool> captureOsd{false}, captureLua{false};
	/* puts that over a picture, at the picture's own size: the window draws
	 * it, so the window sets this and it runs on the window's thread; `osd`
	 * the counters and messages too. Unset, nothing is put over the picture */
	std::function<void(std::vector<uint32_t> &pixels, int32_t width, int32_t height, bool osd)> compose;
	/* idle, encoding, done, stopped, failed */
	Property<std::string> phase{"idle"}, status;
	Property<int64_t> framesDone{0}, frameCount{0};
	Property<double> framesPerSecond{0}, secondsLeft{0};
	/* the sound's peaks lately, 0 to 1 a side: the speakers are quiet while encoding */
	Property<double> levelLeft{0}, levelRight{0};
	/* the file last written, to open */
	Property<std::string> written;

private:
	/* the OSD is drawn over what scripts draw, so it is not had without it (Chimera's two boxes) */
	Connection osdTakesLua_, luaOffTakesOsd_;
	class PresetRows : public RowModel
	{
	public:
		int64_t rowCount() const override { return static_cast<int64_t>(presets().size()); }
		int32_t columnCount() const override { return 2; }
		std::string columnName(int32_t column) const override { return column == 0 ? "name" : column == 1 ? "command" : ""; }
		Value cell(int64_t row, int32_t column) const override
		{
			if (row < 0 || row >= rowCount()) return {};
			const Preset &p = presets()[static_cast<size_t>(row)];
			return std::string(column == 0 ? p.name : p.command);
		}
	};
	struct Job;
	void poll();
	PresetRows presetRows_;
	SessionViewModel &session_;
	std::string appDir_;
	std::shared_ptr<Job> job_;
	Connection commandWatch_;
};

} // namespace chimera::vm
