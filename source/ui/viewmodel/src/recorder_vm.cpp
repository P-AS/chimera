#include "chimera/vm/recorder_vm.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>

#include "chimera/engine.h"

namespace chimera::vm {

namespace fs = std::filesystem;

namespace {
void put32(std::FILE *f, uint32_t v)
{
	const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
	std::fwrite(b, 1, 4, f);
}
void put16(std::FILE *f, uint16_t v)
{
	const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
	std::fwrite(b, 1, 2, f);
}
void fourcc(std::FILE *f, const char *c) { std::fwrite(c, 1, 4, f); }
/* a size written later, at a place kept now */
void patch32(std::FILE *f, long at, uint32_t v)
{
	const long here = std::ftell(f);
	std::fseek(f, at, SEEK_SET);
	put32(f, v);
	std::fseek(f, here, SEEK_SET);
}
constexpr uint32_t kRate = 44100, kBlock = 4;
} // namespace

struct AvWriter::Avi
{
	std::FILE *f = nullptr;
	std::string path;
	int32_t width = 0, height = 0;
	uint32_t frameBytes = 0, rowBytes = 0;
	long riffSize = 0, totalFrames = 0, videoLength = 0, audioLength = 0, moviSize = 0, moviStart = 0;
	uint32_t frames = 0, audioPairs = 0;
	struct Entry
	{
		char id[4];
		uint32_t flags, offset, size;
	};
	std::vector<Entry> index;
	std::vector<uint8_t> row;
};

AvWriter::AvWriter(std::string format, std::string path, PictureEncoder encoder)
	: format_(std::move(format)), path_(std::move(path)), encoder_(std::move(encoder))
{
	const fs::path p(path_);
	base_ = (p.parent_path() / p.stem()).string();
	ext_ = p.extension().string();
}

AvWriter::~AvWriter() { close(); }

bool AvWriter::openWav(const std::string &path, std::string &error)
{
	wav_ = std::fopen(path.c_str(), "wb");
	if (wav_ == nullptr)
	{
		error = "cannot write " + path;
		return false;
	}
	fourcc(wav_, "RIFF");
	put32(wav_, 0); /* patched */
	fourcc(wav_, "WAVE");
	fourcc(wav_, "fmt ");
	put32(wav_, 16);
	put16(wav_, 1);
	put16(wav_, 2);
	put32(wav_, kRate);
	put32(wav_, kRate * kBlock);
	put16(wav_, kBlock);
	put16(wav_, 16);
	fourcc(wav_, "data");
	put32(wav_, 0); /* patched */
	wavBytes_ = 0;
	made_.push_back(path);
	return true;
}

void AvWriter::closeWav()
{
	if (wav_ == nullptr) return;
	patch32(wav_, 4, static_cast<uint32_t>(36 + wavBytes_));
	patch32(wav_, 40, static_cast<uint32_t>(wavBytes_));
	std::fclose(wav_);
	wav_ = nullptr;
}

bool AvWriter::openAvi(int32_t width, int32_t height, std::string &error)
{
	auto a = std::make_unique<Avi>();
	a->path = segment_ == 0 ? base_ + (ext_.empty() ? ".avi" : ext_) : base_ + "_" + std::to_string(segment_) + (ext_.empty() ? ".avi" : ext_);
	a->f = std::fopen(a->path.c_str(), "wb");
	if (a->f == nullptr)
	{
		error = "cannot write " + a->path;
		return false;
	}
	a->width = width;
	a->height = height;
	a->rowBytes = (static_cast<uint32_t>(width) * 3 + 3) & ~3u;
	a->frameBytes = a->rowBytes * static_cast<uint32_t>(height);
	a->row.resize(a->rowBytes);
	std::FILE *f = a->f;
	const uint32_t usPerFrame = static_cast<uint32_t>(std::llround(1000000.0 * static_cast<double>(fpsDen_) / static_cast<double>(fpsNum_)));
	fourcc(f, "RIFF");
	a->riffSize = std::ftell(f);
	put32(f, 0);
	fourcc(f, "AVI ");
	/* the headers: the main one, then a video stream and a sound stream */
	fourcc(f, "LIST");
	put32(f, 4 + (8 + 56) + (12 + (8 + 56) + (8 + 40)) + (12 + (8 + 56) + (8 + 18)));
	fourcc(f, "hdrl");
	fourcc(f, "avih");
	put32(f, 56);
	put32(f, usPerFrame);
	put32(f, static_cast<uint32_t>(a->frameBytes * fpsNum_ / std::max<int64_t>(fpsDen_, 1) + kRate * kBlock));
	put32(f, 0);
	put32(f, 0x10 | 0x100); /* an index, interleaved */
	a->totalFrames = std::ftell(f);
	put32(f, 0);
	put32(f, 0);
	put32(f, 2);
	put32(f, a->frameBytes);
	put32(f, static_cast<uint32_t>(width));
	put32(f, static_cast<uint32_t>(height));
	for (int i = 0; i < 4; i++) put32(f, 0);
	/* the picture: 24-bit, bottom row first, as an uncompressed DIB is */
	fourcc(f, "LIST");
	put32(f, 4 + (8 + 56) + (8 + 40));
	fourcc(f, "strl");
	fourcc(f, "strh");
	put32(f, 56);
	fourcc(f, "vids");
	fourcc(f, "DIB ");
	put32(f, 0);
	put16(f, 0);
	put16(f, 0);
	put32(f, 0);
	put32(f, static_cast<uint32_t>(fpsDen_));
	put32(f, static_cast<uint32_t>(fpsNum_));
	put32(f, 0);
	a->videoLength = std::ftell(f);
	put32(f, 0);
	put32(f, a->frameBytes);
	put32(f, 0xFFFFFFFFu);
	put32(f, 0);
	put16(f, 0);
	put16(f, 0);
	put16(f, static_cast<uint16_t>(width));
	put16(f, static_cast<uint16_t>(height));
	fourcc(f, "strf");
	put32(f, 40);
	put32(f, 40);
	put32(f, static_cast<uint32_t>(width));
	put32(f, static_cast<uint32_t>(height));
	put16(f, 1);
	put16(f, 24);
	put32(f, 0);
	put32(f, a->frameBytes);
	for (int i = 0; i < 4; i++) put32(f, 0);
	/* the sound: 16-bit stereo PCM at 44,100 Hz */
	fourcc(f, "LIST");
	put32(f, 4 + (8 + 56) + (8 + 18));
	fourcc(f, "strl");
	fourcc(f, "strh");
	put32(f, 56);
	fourcc(f, "auds");
	put32(f, 0);
	put32(f, 0);
	put16(f, 0);
	put16(f, 0);
	put32(f, 0);
	put32(f, kBlock);
	put32(f, kRate * kBlock);
	put32(f, 0);
	a->audioLength = std::ftell(f);
	put32(f, 0);
	put32(f, kRate * kBlock);
	put32(f, 0xFFFFFFFFu);
	put32(f, kBlock);
	for (int i = 0; i < 4; i++) put16(f, 0);
	fourcc(f, "strf");
	put32(f, 18);
	put16(f, 1);
	put16(f, 2);
	put32(f, kRate);
	put32(f, kRate * kBlock);
	put16(f, kBlock);
	put16(f, 16);
	put16(f, 0);
	fourcc(f, "LIST");
	a->moviSize = std::ftell(f);
	put32(f, 0);
	a->moviStart = std::ftell(f);
	fourcc(f, "movi");
	if (std::ferror(f))
	{
		error = "cannot write " + a->path;
		std::fclose(f);
		return false;
	}
	made_.push_back(a->path);
	avi_ = std::move(a);
	return true;
}

void AvWriter::closeAvi()
{
	if (!avi_) return;
	Avi &a = *avi_;
	std::FILE *f = a.f;
	const long moviEnd = std::ftell(f);
	fourcc(f, "idx1");
	put32(f, static_cast<uint32_t>(a.index.size() * 16));
	for (const Avi::Entry &e : a.index)
	{
		std::fwrite(e.id, 1, 4, f);
		put32(f, e.flags);
		put32(f, e.offset);
		put32(f, e.size);
	}
	const long end = std::ftell(f);
	patch32(f, a.riffSize, static_cast<uint32_t>(end - 8));
	patch32(f, a.moviSize, static_cast<uint32_t>(moviEnd - a.moviStart));
	patch32(f, a.totalFrames, a.frames);
	patch32(f, a.videoLength, a.frames);
	patch32(f, a.audioLength, a.audioPairs);
	std::fclose(f);
	avi_.reset();
}

bool AvWriter::frame(const uint32_t *pixels, int32_t width, int32_t height, const int16_t *samples, int32_t pairs, int64_t fpsNum,
	int64_t fpsDen, std::string &error)
{
	if (fpsNum > 0 && fpsDen > 0 && frames_ == 0)
	{
		fpsNum_ = fpsNum;
		fpsDen_ = fpsDen;
	}
	const bool picture = pixels != nullptr && width > 0 && height > 0;
	if (format_ == "wav" || format_ == "png")
	{
		if (wav_ == nullptr && !openWav(format_ == "wav" ? path_ : base_ + ".wav", error)) return false;
		if (samples != nullptr && pairs > 0)
		{
			std::fwrite(samples, kBlock, static_cast<size_t>(pairs), wav_);
			wavBytes_ += static_cast<uint64_t>(pairs) * kBlock;
		}
		if (format_ == "png" && picture)
		{
			char number[16];
			std::snprintf(number, sizeof number, "_%06lld", static_cast<long long>(frames_));
			const std::string file = base_ + number + ".png";
			if (!encoder_ || !encoder_(file, pixels, width, height))
			{
				error = "cannot write " + file;
				return false;
			}
			made_.push_back(file);
		}
		frames_++;
		return true;
	}
	/* avi: a new file at a gigabyte, and when the picture changes size */
	if (picture && avi_ && (avi_->width != width || avi_->height != height || static_cast<uint64_t>(std::ftell(avi_->f)) >= segmentBytes))
	{
		closeAvi();
		segment_++;
	}
	if (!avi_)
	{
		if (!picture) return true; /* nothing to size a file by yet */
		if (!openAvi(width, height, error)) return false;
	}
	Avi &a = *avi_;
	if (picture)
	{
		const long at = std::ftell(a.f);
		fourcc(a.f, "00dc");
		put32(a.f, a.frameBytes);
		for (int32_t y = height - 1; y >= 0; y--)
		{
			const uint32_t *src = pixels + static_cast<size_t>(y) * static_cast<size_t>(width);
			std::memset(a.row.data(), 0, a.row.size());
			for (int32_t x = 0; x < width; x++)
			{
				a.row[static_cast<size_t>(x) * 3] = static_cast<uint8_t>(src[x]);
				a.row[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>(src[x] >> 8);
				a.row[static_cast<size_t>(x) * 3 + 2] = static_cast<uint8_t>(src[x] >> 16);
			}
			std::fwrite(a.row.data(), 1, a.row.size(), a.f);
		}
		a.index.push_back({{'0', '0', 'd', 'c'}, 0x10, static_cast<uint32_t>(at - a.moviStart), a.frameBytes});
		a.frames++;
	}
	if (samples != nullptr && pairs > 0)
	{
		const long at = std::ftell(a.f);
		const uint32_t bytes = static_cast<uint32_t>(pairs) * kBlock;
		fourcc(a.f, "01wb");
		put32(a.f, bytes);
		std::fwrite(samples, 1, bytes, a.f);
		a.index.push_back({{'0', '1', 'w', 'b'}, 0x10, static_cast<uint32_t>(at - a.moviStart), bytes});
		a.audioPairs += static_cast<uint32_t>(pairs);
	}
	if (std::ferror(a.f))
	{
		error = "cannot write " + a.path + " (is the disk full?)";
		return false;
	}
	frames_++;
	return true;
}

std::vector<std::string> AvWriter::close()
{
	closeAvi();
	closeWav();
	return made_;
}

RecorderViewModel::RecorderViewModel(Dispatcher &dispatcher, SessionViewModel &session, AvWriter::PictureEncoder encoder)
	: ViewModel("recorder", dispatcher), session_(session), encoder_(std::move(encoder))
{
	expose("recording", "every frame run is being written", recording);
	expose("frames", "frames written so far", frames);
	expose("format", "what is written: avi (picture and sound, uncompressed), wav (sound), png (a picture a frame and a wav)",
		format, true);
	expose("status", "what the recorder last did", status);
	command("start", "records every frame run from now into this file, in the format chosen", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			if (recording.get())
			{
				error = "already recording";
				return {};
			}
			if (format.get() != "avi" && format.get() != "wav" && format.get() != "png")
			{
				error = "no format " + format.get() + ": avi, wav or png";
				return {};
			}
			if (!session_.isOpen.get())
			{
				error = "no core is running";
				return {};
			}
			writer_ = std::make_shared<AvWriter>(format.get(), path, encoder_);
			writer_->segmentBytes = segmentBytes;
			std::weak_ptr<AvWriter> weak = writer_;
			auto failed = std::make_shared<bool>(false);
			hook_ = session_.addMachineHooks(
				[this, weak, failed](ce_session *s) {
					std::shared_ptr<AvWriter> w = weak.lock();
					if (!w || *failed) return;
					int32_t pairs = 0;
					const int16_t *samples = ce_session_audio(s, &pairs);
					const uint32_t *px = ce_session_video(s);
					std::string why;
					if (!w->frame(px, ce_session_video_width(s), ce_session_video_height(s), samples, pairs, ce_session_vsync_numerator(s),
							ce_session_vsync_denominator(s), why))
					{
						*failed = true;
						later([this, why] { stopNow("Recording stopped: " + why); });
						return;
					}
					/* said now and then, not each frame */
					if (w->frames() % 30 == 0) later([this, n = w->frames()] { frames.set(n); });
				},
				[this](ce_session *) { later([this] { stopNow("Recording stopped: the machine closed"); }); });
			session_.setRecording(true);
			recording.set(true);
			frames.set(0);
			status.set("Recording to " + path);
			return {};
		}, &session_.isOpen);
	command("stop", "the recording finished and closed; says the files made", {}, [this](const Args &, std::string &) -> Value {
		stopNow("");
		return status.get();
	});
}

RecorderViewModel::~RecorderViewModel()
{
	if (hook_ != 0) session_.removeMachineHooks(hook_);
	session_.setRecording(false);
}

void RecorderViewModel::stopNow(const std::string &why)
{
	if (!recording.get()) return;
	/* the hook gone first: nothing writes while the files close */
	if (hook_ != 0) session_.removeMachineHooks(hook_);
	hook_ = 0;
	session_.setRecording(false);
	std::vector<std::string> made;
	int64_t n = 0;
	if (writer_)
	{
		n = writer_->frames();
		made = writer_->close();
	}
	writer_.reset();
	recording.set(false);
	frames.set(n);
	std::string list;
	for (const std::string &m : made) list += (list.empty() ? "" : ", ") + fs::path(m).filename().string();
	status.set(why.empty() ? "Recorded " + std::to_string(n) + " frames: " + (list.empty() ? std::string("nothing written") : list)
						   : why + " (" + std::to_string(n) + " frames: " + list + ")");
}

} // namespace chimera::vm
