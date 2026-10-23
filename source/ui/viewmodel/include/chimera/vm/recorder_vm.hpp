#pragma once
/* Chimera's AVI/WAV recording (File > AVI/WAV), for what can be written
 * without a codec of the system's: every frame the machine runs while
 * recording, written as it runs, on the machine's thread -
 *  - "avi": an uncompressed AVI, the picture (24-bit) and the sound (16-bit
 *    stereo, 44,100 Hz), a new file each gigabyte and at each change of the
 *    picture's size (name_1.avi, ...), as Chimera splits;
 *  - "wav": the sound alone, a RIFF WAV;
 *  - "png": a picture a frame (name_000000.png, ...) and the sound in
 *    name.wav beside them, Chimera's image sequence; the window hands in
 *    the picture encoder.
 * While recording every frame is drawn, whatever turbo, frame skip or
 * Display Nothing say. */

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

/* one recording, fed frame by frame (any one thread) */
class AvWriter
{
public:
	using PictureEncoder = std::function<bool(const std::string &path, const uint32_t *pixels, int32_t width, int32_t height)>;
	/* format "avi", "wav" or "png" */
	AvWriter(std::string format, std::string path, PictureEncoder encoder);
	~AvWriter();
	/* a frame's picture (BGRA, may be null), its sound (stereo pairs) and
	 * the machine's frame rate as a fraction; false with the reason when
	 * writing failed */
	bool frame(const uint32_t *pixels, int32_t width, int32_t height, const int16_t *samples, int32_t pairs, int64_t fpsNum,
		int64_t fpsDen, std::string &error);
	/* everything written out and closed; the files made */
	std::vector<std::string> close();
	int64_t frames() const { return frames_; }
	/* the most an AVI segment may hold before the next one starts (a test makes it small) */
	uint64_t segmentBytes = 1000ull * 1000ull * 1000ull;

private:
	struct Avi;
	bool openAvi(int32_t width, int32_t height, std::string &error);
	void closeAvi();
	bool openWav(const std::string &path, std::string &error);
	void closeWav();
	std::string format_, path_, base_, ext_;
	int64_t fpsNum_ = 60, fpsDen_ = 1, frames_ = 0;
	PictureEncoder encoder_;
	std::unique_ptr<Avi> avi_;
	int segment_ = 0;
	std::FILE *wav_ = nullptr;
	uint64_t wavBytes_ = 0;
	std::vector<std::string> made_;
};

class RecorderViewModel : public ViewModel
{
public:
	RecorderViewModel(Dispatcher &dispatcher, SessionViewModel &session, AvWriter::PictureEncoder encoder);
	~RecorderViewModel() override;

	Property<bool> recording{false};
	Property<int64_t> frames{0};
	Property<std::string> format{"avi"}, status;
	/* for a test: the size an AVI segment may reach */
	uint64_t segmentBytes = 1000ull * 1000ull * 1000ull;

private:
	void stopNow(const std::string &why);
	SessionViewModel &session_;
	AvWriter::PictureEncoder encoder_;
	std::shared_ptr<AvWriter> writer_;
	int hook_ = 0;
	Connection closeWatch_;
};

} // namespace chimera::vm
