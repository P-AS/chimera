#pragma once
/* The NUT container, written as a stream: the picture as raw BGRA and the
 * sound as 16-bit PCM, interleaved by time - what the encode hands ffmpeg on
 * its standard input. Chimera's NUT muxer. */

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace chimera::vm {

class NutMuxer
{
public:
	using Sink = std::function<bool(const uint8_t *data, size_t size)>;
	/* the frame rate as a fraction, the sound's rate and channels; the
	 * headers are written at once */
	NutMuxer(int32_t width, int32_t height, int64_t fpsNum, int64_t fpsDen, int32_t sampleRate, int32_t channels, Sink sink);
	/* a frame's pixels, width * height of them */
	bool writeVideo(const uint32_t *pixels, size_t count);
	/* interleaved samples, a channel each in turn */
	bool writeAudio(const int16_t *samples, size_t count);
	/* the ends of both streams, and everything still waiting */
	bool finish();
	bool failed() const { return failed_; }

private:
	struct Frame
	{
		std::vector<uint8_t> bytes;
		uint64_t pts, num, den;
	};
	static bool atOrBefore(const Frame &a, const Frame &b);
	Frame makeFrame(const uint8_t *payload, size_t size, uint64_t pts, uint64_t num, uint64_t den, int streamIndex);
	bool emit(const std::vector<uint8_t> &bytes);
	Sink sink_;
	int64_t fpsNum_, fpsDen_;
	int32_t sampleRate_, channels_;
	uint64_t videoPts_ = 0, audioPts_ = 0;
	bool videoDone_ = false, audioDone_ = false, failed_ = false;
	std::deque<Frame> videoQueue_, audioQueue_;
};

} // namespace chimera::vm
