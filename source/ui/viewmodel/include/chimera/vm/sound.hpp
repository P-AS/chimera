#pragma once
/* The machine's sound to the speakers, through OpenAL Soft found at run time
 * (Chimera ships it, and the C# interface plays through it): each frame's
 * samples are queued as the frame finishes, at 44,100 Hz. The machine keeps
 * its own time by the clock; when the queue runs too far ahead (the machine
 * ran faster than the speakers) a frame's sound is dropped rather than heard
 * late. Without OpenAL or a sound device it is silent, and says why.
 *
 * One thread only: the one that made it. */

#include <cstdint>
#include <memory>
#include <string>

namespace chimera::vm {

class Sound
{
public:
	static constexpr int kRate = 44100;

	Sound();
	~Sound();
	Sound(const Sound &) = delete;
	Sound &operator=(const Sound &) = delete;

	bool available() const;
	const std::string &problem() const { return problem_; }

	/* interleaved stereo, `pairs` sample pairs */
	void play(const int16_t *samples, int32_t pairs);

	/* sample pairs queued since it opened, and those dropped for running ahead */
	int64_t queued() const { return queued_; }
	int64_t dropped() const { return dropped_; }

private:
	struct Api;
	std::unique_ptr<Api> api_;
	std::string problem_;
	int64_t queued_ = 0, dropped_ = 0;
};

} // namespace chimera::vm
