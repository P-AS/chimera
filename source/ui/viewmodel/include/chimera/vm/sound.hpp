#pragma once
/* The machine's sound to the speakers, through OpenAL Soft found at run time
 * (Chimera ships it, and the C# interface plays through it): each frame's
 * samples are queued as the frame finishes, at 44,100 Hz. The machine keeps
 * its own time by the clock; when the queue runs further ahead than the
 * buffer (Chimera's buffer size, 100 ms at first) a frame's sound is dropped
 * rather than heard late. It plays on the device named, or the system's
 * own. Without OpenAL or a sound device it is silent, and says why.
 *
 * One thread only: the one that made it. */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chimera::vm {

class Sound
{
public:
	static constexpr int kRate = 44100;

	/* on the device named ("" the system's own), with a buffer of so many
	 * milliseconds */
	explicit Sound(const std::string &device = "", int32_t bufferMs = 100);
	~Sound();
	Sound(const Sound &) = delete;
	Sound &operator=(const Sound &) = delete;

	bool available() const;
	const std::string &problem() const { return problem_; }
	/* the sound devices OpenAL knows, by name */
	std::vector<std::string> devices() const;

	/* interleaved stereo, `pairs` sample pairs, at a volume from 0 (not
	 * played) to 100 (as the machine made them) */
	void play(const int16_t *samples, int32_t pairs, int32_t volume = 100);
	/* the samples at a volume (0 to 100), into out */
	static void scale(const int16_t *samples, int32_t pairs, int32_t volume, std::vector<int16_t> &out);

	/* sample pairs queued since it opened, and those dropped for running ahead */
	int64_t queued() const { return queued_; }
	/* sample pairs queued and not heard yet (what has been heard let go first) */
	int64_t ahead();
	int64_t dropped() const { return dropped_; }

private:
	struct Api;
	std::unique_ptr<Api> api_;
	std::string problem_;
	int64_t queued_ = 0, dropped_ = 0;
	/* sample pairs waiting to be heard, and the most that may */
	int64_t ahead_ = 0, mostAhead_ = 0;
	std::vector<int16_t> scaled_;
};

} // namespace chimera::vm
