#include "chimera/vm/encoder_vm.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <mutex>

#include "chimera/engine.h"
#include "chimera/vm/nut_muxer.hpp"
#include "chimera/vm/subprocess.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

const char *const EncoderViewModel::kDefaultCommand = "-c:v libx264 -c:a aac -pix_fmt yuv420p -f mp4";

const std::vector<EncoderViewModel::Preset> &EncoderViewModel::presets()
{
	/* Chimera's (its issue 323): the default, four for YouTube at 2160 lines, four that keep every pixel */
	static const std::vector<Preset> list = {
		{"MP4, H.264 and AAC (the default)", kDefaultCommand},
		{"YouTube 2160p, landscape, smooth scaling", "-vf scale=-2:2160:flags=lanczos -c:v libx264 -crf 12 -pix_fmt yuv420p -c:a aac -b:a 384k -f mp4"},
		{"YouTube 2160p, landscape, sharp pixels", "-vf scale=-2:2160:flags=neighbor -c:v libx264 -crf 12 -pix_fmt yuv420p -c:a aac -b:a 384k -f mp4"},
		{"YouTube 2160p, portrait, smooth scaling", "-vf scale=2160:-2:flags=lanczos -c:v libx264 -crf 12 -pix_fmt yuv420p -c:a aac -b:a 384k -f mp4"},
		{"YouTube 2160p, portrait, sharp pixels", "-vf scale=2160:-2:flags=neighbor -c:v libx264 -crf 12 -pix_fmt yuv420p -c:a aac -b:a 384k -f mp4"},
		{"Lossless H.264, MKV", "-c:v libx264 -qp 0 -c:a aac -f matroska"},
		{"Lossless H.264 RGB, MKV", "-c:v libx264rgb -qp 0 -pix_fmt rgb24 -c:a aac -f matroska"},
		{"Lossless H.265, MKV", "-c:v libx265 -x265-params lossless=1 -pix_fmt yuv444p -c:a aac -f matroska"},
		{"Lossless UT Video, MKV", "-c:v utvideo -pix_fmt gbrp -c:a aac -f matroska"},
	};
	return list;
}

std::string EncoderViewModel::extensionOf(const std::string &command)
{
	const std::vector<std::string> words = splitCommandLine(command);
	for (size_t i = 0; i + 1 < words.size(); i++)
		if (words[i] == "-f") return words[i + 1] == "matroska" ? "mkv" : words[i + 1];
	return {};
}

namespace {
constexpr int32_t kRate = 44100;

/* the picture at another size: stretched to it, or kept in shape with black
 * bars (pad); nearest pixel, so pixel art stays sharp */
void scaleInto(const uint32_t *src, int32_t sw, int32_t sh, std::vector<uint32_t> &dst, int32_t dw, int32_t dh, bool padded)
{
	dst.assign(static_cast<size_t>(dw) * dh, 0xFF000000u);
	int32_t ox = 0, oy = 0, w = dw, h = dh;
	if (padded && sw > 0 && sh > 0)
	{
		if (static_cast<int64_t>(dw) * sh > static_cast<int64_t>(dh) * sw) w = static_cast<int32_t>(static_cast<int64_t>(dh) * sw / sh);
		else h = static_cast<int32_t>(static_cast<int64_t>(dw) * sh / sw);
		ox = (dw - w) / 2;
		oy = (dh - h) / 2;
	}
	for (int32_t y = 0; y < h; y++)
	{
		const int32_t sy = static_cast<int32_t>(static_cast<int64_t>(y) * sh / h);
		for (int32_t x = 0; x < w; x++)
			dst[static_cast<size_t>(y + oy) * dw + (x + ox)] = src[static_cast<size_t>(sy) * sw + static_cast<int32_t>(static_cast<int64_t>(x) * sw / w)] | 0xFF000000u;
	}
}
} // namespace

/* one encode, its writing on the machine's thread */
struct EncoderViewModel::Job
{
	std::string ffmpeg, base, ext, command;
	int32_t outW = 0, outH = 0;
	bool resize = false, pad = false, audioSync = true;
	/* what is drawn over the picture, put in by the window (Capture OSD, Capture Lua) */
	bool osd = false, lua = false;
	std::function<void(std::vector<uint32_t> &, int32_t, int32_t, bool)> compose;
	SessionViewModel *session = nullptr;
	std::vector<uint32_t> composed;
	int64_t from = 0, to = 0;
	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

	/* the current file */
	std::unique_ptr<Subprocess> process;
	std::unique_ptr<NutMuxer> muxer;
	int32_t w = 0, h = 0;
	int segment = 0;
	std::vector<std::string> files;
	/* the stretchers' arithmetic (Chimera's AVSync) */
	int64_t fpsNum = 60, fpsDen = 1, soundRemainder = 0, extraAudio = 0;
	std::vector<int16_t> fifo, chunk;
	std::vector<uint32_t> scaled;
	std::mutex levelsM;
	double peakL = 0, peakR = 0;
	std::string error;

	std::string fileName() const { return base + (segment == 0 ? "" : "_" + std::to_string(segment)) + ext; }

	bool open(std::string &why)
	{
		process = std::make_unique<Subprocess>();
		std::vector<std::string> args = {"-y", "-f", "nut", "-i", "-"};
		for (const std::string &word : splitCommandLine(command)) args.push_back(word);
		args.push_back(fileName());
		if (!process->start(ffmpeg, args, why)) return false;
		Subprocess *p = process.get();
		muxer = std::make_unique<NutMuxer>(w, h, fpsNum, fpsDen, kRate, 2, [p](const uint8_t *d, size_t n) { return p->write(d, n); });
		files.push_back(fileName());
		if (!muxer->failed()) return true;
		why = died();
		return false;
	}
	std::string died()
	{
		if (!process) return "ffmpeg is not running";
		process->closeInput();
		process->wait(5000);
		const std::string tail = process->errorTail();
		return "ffmpeg stopped" + (tail.empty() ? std::string(".") : ":\n" + tail);
	}
	/* finishes the current file; false with why when ffmpeg did not end well */
	bool close(std::string &why)
	{
		if (!muxer) return true;
		const bool fed = muxer->finish();
		muxer.reset();
		process->closeInput();
		/* how long ffmpeg takes to finish the file, at most */
		int code = process->wait(20000);
		if (code < 0)
		{
			process->kill();
			code = process->wait(-1);
		}
		const std::string tail = process->errorTail();
		process.reset();
		if (fed && code == 0) return true;
		why = "ffmpeg ended with " + std::to_string(code) + (tail.empty() ? std::string(".") : ":\n" + tail);
		return false;
	}
	bool video(const uint32_t *px, std::string &why)
	{
		if (!muxer->writeVideo(px, static_cast<size_t>(w) * h))
		{
			why = died();
			return false;
		}
		return true;
	}
	bool audio(const int16_t *s, size_t n, std::string &why)
	{
		if (!muxer->writeAudio(s, n))
		{
			why = died();
			return false;
		}
		return true;
	}

	bool frame(ce_session *s, std::string &why)
	{
		const uint32_t *px = ce_session_video(s);
		int32_t sw = ce_session_video_width(s), sh = ce_session_video_height(s);
		int32_t pairs = 0;
		const int16_t *samples = ce_session_audio(s, &pairs);
		if (px == nullptr || sw <= 0 || sh <= 0)
		{
			why = "The machine drew no picture.";
			return false;
		}
		/* what is drawn over it, at its own size, by the window: then made
		 * the size asked for, as Chimera captures the OSD before it scales */
		if ((osd || lua) && compose && session != nullptr)
		{
			composed.assign(px, px + static_cast<size_t>(sw) * sh);
			for (uint32_t &p : composed) p |= 0xFF000000u;
			if (!session->onWindowWait([&] { compose(composed, sw, sh, osd); }))
			{
				why = "The window is closing.";
				return false;
			}
			px = composed.data();
		}
		/* the picture at the size asked for */
		const uint32_t *pic = px;
		int32_t pw = sw, ph = sh;
		if (resize && outW > 0 && outH > 0)
		{
			scaleInto(px, sw, sh, scaled, outW, outH, pad);
			pic = scaled.data();
			pw = outW;
			ph = outH;
		}
		else
		{
			scaled.assign(px, px + static_cast<size_t>(sw) * sh);
			for (uint32_t &p : scaled) p |= 0xFF000000u;
			pic = scaled.data();
		}
		/* the first frame says the size; a new size, a new file */
		if (!muxer || pw != w || ph != h)
		{
			if (muxer)
			{
				if (!close(why)) return false;
				segment++;
			}
			w = pw;
			h = ph;
			fpsNum = ce_session_vsync_numerator(s);
			fpsDen = ce_session_vsync_denominator(s);
			if (fpsNum <= 0 || fpsDen <= 0) fpsNum = 60, fpsDen = 1;
			if (!open(why)) return false;
		}
		/* the sound's peaks, for the meters */
		{
			int16_t l = 0, r = 0;
			for (int32_t i = 0; i < pairs; i++)
			{
				l = std::max<int16_t>(l, static_cast<int16_t>(std::min(32767, std::abs(static_cast<int>(samples[2 * i])))));
				r = std::max<int16_t>(r, static_cast<int16_t>(std::min(32767, std::abs(static_cast<int>(samples[2 * i + 1])))));
			}
			std::lock_guard<std::mutex> lock(levelsM);
			peakL = std::max(peakL, l / 32767.0);
			peakR = std::max(peakR, r / 32767.0);
		}
		if (audioSync)
		{
			/* the sound as it came, the picture dropped or doubled to keep up */
			const int64_t total = fpsDen * kRate, one = total * 4 / 10, more = total * 9 / 10;
			extraAudio += static_cast<int64_t>(pairs) * fpsNum;
			if (extraAudio >= one)
			{
				if (!video(pic, why)) return false;
				extraAudio -= total;
			}
			while (extraAudio >= more)
			{
				if (!video(pic, why)) return false;
				extraAudio -= total;
			}
			return pairs == 0 || audio(samples, static_cast<size_t>(pairs) * 2, why);
		}
		/* a frame each time, and exactly a frame's worth of sound with it */
		if (!video(pic, why)) return false;
		const int64_t num = static_cast<int64_t>(kRate) * fpsDen + soundRemainder;
		const int64_t want = num / fpsNum;
		soundRemainder = num % fpsNum;
		if (samples != nullptr && pairs > 0) fifo.insert(fifo.end(), samples, samples + static_cast<size_t>(pairs) * 2);
		chunk.assign(static_cast<size_t>(want) * 2, 0);
		const size_t have = std::min(chunk.size(), fifo.size());
		std::copy(fifo.begin(), fifo.begin() + static_cast<std::ptrdiff_t>(have), chunk.begin());
		fifo.erase(fifo.begin(), fifo.begin() + static_cast<std::ptrdiff_t>(have));
		/* more than a second behind: the oldest goes */
		if (fifo.size() > static_cast<size_t>(kRate) * 2) fifo.erase(fifo.begin(), fifo.end() - kRate * 2);
		return audio(chunk.data(), chunk.size(), why);
	}
};

EncoderViewModel::EncoderViewModel(Dispatcher &dispatcher, SessionViewModel &session, std::string appDir)
	: ViewModel("encoder", dispatcher), session_(session), appDir_(std::move(appDir))
{
	expose("output", "the file the video is written to; its extension follows the command's container", output, true);
	expose("command", "the ffmpeg command that fills it (Chimera's preset or one's own)", commandLine, true);
	expose("preset", "the preset the command is, or \"Your own command\"", preset);
	expose("fromFrame", "the first frame encoded (a marker's)", fromFrame, true);
	expose("toFrame", "the last frame encoded, included (a marker's)", toFrame, true);
	expose("resize", "the picture made another size", resize, true);
	expose("width", "that size's width", width, true);
	expose("height", "that size's height", height, true);
	expose("pad", "kept in shape with black bars, not stretched", pad, true);
	expose("audioSync", "the sound kept whole, the picture's frames dropped or doubled to match it", audioSync, true);
	expose("captureOsd", "the counters, messages and what scripts draw are in the video (Chimera's Capture OSD)", captureOsd, true);
	expose("captureLua", "what scripts draw is in the video (Chimera's Capture Lua)", captureLua, true);
	osdTakesLua_ = captureOsd.changed.connect([this] {
		if (captureOsd.get()) captureLua.set(true);
	});
	luaOffTakesOsd_ = captureLua.changed.connect([this] {
		if (!captureLua.get()) captureOsd.set(false);
	});
	expose("phase", "idle, encoding, done, stopped or failed", phase);
	expose("status", "how it is going, or how it went", status);
	expose("framesDone", "frames written", framesDone);
	expose("frameCount", "frames to write", frameCount);
	expose("framesPerSecond", "frames written a second", framesPerSecond);
	expose("secondsLeft", "roughly how much longer", secondsLeft);
	expose("levelLeft", "the sound's left peak lately, 0 to 1", levelLeft);
	expose("levelRight", "the sound's right peak lately, 0 to 1", levelRight);
	expose("written", "the file last written", written);
	rows("presets", "Chimera's presets: name, command", presetRows_);

	auto follow = [this] {
		/* the box shows the preset the command is; the file the container's extension */
		const std::string c = commandLine.get();
		std::string name = "Your own command";
		for (const Preset &p : presets())
			if (c == p.command) name = p.name;
		preset.set(name);
		const std::string ext = extensionOf(c);
		if (!ext.empty() && !output.get().empty())
		{
			fs::path p(output.get());
			if (p.extension().string() != "." + ext)
			{
				p.replace_extension(ext);
				output.set(p.string());
			}
		}
	};
	follow();
	commandWatch_ = commandLine.changed.connect(follow);

	command("usePreset", "the preset's command into the command box", {"index"}, [this](const Args &a, std::string &error) -> Value {
		int64_t i = 0;
		if (!arg(a, 0, i, error)) return {};
		if (i < 0 || i >= static_cast<int64_t>(presets().size()))
		{
			error = "no preset " + std::to_string(i);
			return {};
		}
		commandLine.set(presets()[static_cast<size_t>(i)].command);
		return {};
	});
	command("restoreDefaults", "the command, sync and size back to a fresh install's (the file and markers stay)", {},
		[this](const Args &, std::string &) -> Value {
			commandLine.set(kDefaultCommand);
			audioSync.set(true);
			pad.set(false);
			resize.set(false);
			return {};
		});
	command("suggest", "the file to write: the folder and name given, the command's extension", {"folder", "name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string folder, name;
			if (!arg(a, 0, folder, error) || !arg(a, 1, name, error)) return {};
			const std::string ext = extensionOf(commandLine.get());
			output.set((fs::path(folder) / (name + "." + (ext.empty() ? std::string("mp4") : ext))).string());
			return output.get();
		});
	command("ffmpegPath", "the ffmpeg the encode runs, \"\" when there is none", {}, [this](const Args &, std::string &) -> Value {
		return ffmpegPath();
	});
	command("start", "winds the run to the first frame, encodes to the last, and gives the machine back", {},
		[this](const Args &, std::string &error) -> Value {
			if (job_)
			{
				error = "An encode is already running.";
				return {};
			}
			if (!session_.hasProject.get())
			{
				error = "Open a project first: a video is an encode of a run.";
				return {};
			}
			if (toFrame.get() < fromFrame.get())
			{
				error = "The end marker is before the start marker.";
				return {};
			}
			if (output.get().empty())
			{
				error = "Choose a file to write the video to.";
				return {};
			}
			if (splitCommandLine(commandLine.get()).empty())
			{
				error = "The ffmpeg command is empty.";
				return {};
			}
			const std::string ff = ffmpegPath();
			if (ff.empty())
			{
				error = "There is no ffmpeg beside Chimera (it ships in the bundle's dll folder).";
				return {};
			}
			const fs::path out(output.get());
			if (out.has_parent_path() && !fs::is_directory(out.parent_path()))
			{
				error = "There is no folder " + out.parent_path().string() + ".";
				return {};
			}
			auto job = std::make_shared<Job>();
			job->ffmpeg = ff;
			job->ext = out.extension().string();
			job->base = (out.parent_path() / out.stem()).string();
			job->command = commandLine.get();
			job->resize = resize.get();
			job->outW = static_cast<int32_t>(width.get());
			job->outH = static_cast<int32_t>(height.get());
			job->pad = pad.get();
			job->audioSync = audioSync.get();
			/* as Chimera's: the OSD takes what scripts draw with it */
			job->osd = captureOsd.get();
			job->lua = captureLua.get() || job->osd;
			job->compose = compose;
			job->session = &session_;
			job->from = fromFrame.get();
			job->to = toFrame.get();
			if (job->resize && (job->outW < 2 || job->outH < 2))
			{
				error = "The size to resize to is too small.";
				return {};
			}
			job_ = job;
			phase.set("encoding");
			framesDone.set(0);
			frameCount.set(job->to - job->from + 1);
			status.set("Winding the run to frame " + std::to_string(job->from) + "...");
			session_.borrowMachine(
				job->from, job->to, [job](ce_session *s, std::string &why) { return job->frame(s, why); },
				[job] {
					std::string why;
					if (!job->close(why) && job->error.empty()) job->error = why;
				},
				[this, job](int ended, std::string why) {
					if (!job->error.empty()) why += (why.empty() ? "" : " ") + job->error;
					const int64_t n = session_.borrowedFrames();
					framesDone.set(n);
					std::string files;
					for (const std::string &f : job->files) files += (files.empty() ? "" : ", ") + fs::path(f).filename().string();
					if (!job->files.empty()) written.set(job->files.front());
					if (ended == 0 && job->error.empty())
					{
						phase.set("done");
						status.set("Wrote " + std::to_string(n) + " frames to " + files + ".");
					}
					else if (ended == 1)
					{
						phase.set("stopped");
						status.set("Stopped after " + std::to_string(n) + " of " + std::to_string(frameCount.get()) + " frames." +
								   (why.empty() ? "" : " " + why));
					}
					else
					{
						phase.set("failed");
						status.set(why.empty() ? std::string("The encode failed.") : why);
					}
					levelLeft.set(0);
					levelRight.set(0);
					job_.reset();
				});
			return {};
		});
	command("stop", "ends the encode after the frame it is on; what was written stays, and plays", {},
		[this](const Args &, std::string &) -> Value {
			if (job_) session_.stopBorrow();
			return {};
		});
	command("poll", "reads how far the encode has got (the window asks five times a second)", {},
		[this](const Args &, std::string &) -> Value {
			poll();
			return {};
		});
}

EncoderViewModel::~EncoderViewModel()
{
	if (job_) session_.stopBorrow();
}

void EncoderViewModel::poll()
{
	if (!job_) return;
	const int64_t n = session_.borrowedFrames();
	framesDone.set(n);
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - job_->started).count();
	const double fps = secs > 0 ? n / secs : 0;
	framesPerSecond.set(fps);
	secondsLeft.set(fps > 0 ? (frameCount.get() - n) / fps : 0);
	if (n > 0) status.set(progressText(n, frameCount.get(), fps));
	double l = 0, r = 0;
	{
		std::lock_guard<std::mutex> lock(job_->levelsM);
		l = job_->peakL;
		r = job_->peakR;
		job_->peakL = job_->peakR = 0;
	}
	/* the meters fall back slowly, as meters do */
	levelLeft.set(std::max(l, levelLeft.get() * 0.6));
	levelRight.set(std::max(r, levelRight.get() * 0.6));
}

std::string EncoderViewModel::progressText(int64_t done, int64_t total, double framesPerSecond)
{
	/* a count as Chimera's N0 writes it: 1,234 */
	auto grouped = [](int64_t n) {
		std::string text = std::to_string(n < 0 ? -n : n);
		for (int at = static_cast<int>(text.size()) - 3; at > 0; at -= 3) text.insert(static_cast<size_t>(at), ",");
		return n < 0 ? "-" + text : text;
	};
	std::string text = "frame " + grouped(done) + " / " + grouped(total) + "   " + grouped(std::llround(framesPerSecond)) + " fps";
	if (framesPerSecond > 0)
	{
		const int64_t left = static_cast<int64_t>(static_cast<double>(std::max<int64_t>(0, total - done)) / framesPerSecond);
		const int64_t h = left / 3600, m = left / 60 % 60, sec = left % 60;
		text += "   about " + (h >= 1 ? std::to_string(h) + "h " + std::to_string(m) + "m"
								 : left >= 60 ? std::to_string(left / 60) + "m " + std::to_string(sec) + "s" : std::to_string(sec) + "s") +
				" remaining";
	}
	return text;
}

std::string EncoderViewModel::ffmpegPath() const
{
	std::error_code ec;
	if (const char *env = std::getenv("CHIMERA_FFMPEG"); env != nullptr && *env != '\0')
		return fs::is_regular_file(env, ec) ? std::string(env) : std::string();
#ifdef _WIN32
	const char *name = "ffmpeg.exe";
#else
	const char *name = "ffmpeg";
#endif
	const fs::path dir(appDir_);
	/* the Qt bundle keeps it in bin/, Chimera's beside the program or in dll/; a build in build/dll */
	for (const fs::path &p : {dir / "bin" / name, dir / name, dir / "dll" / name, dir / ".." / ".." / ".." / "dll" / name})
		if (fs::is_regular_file(p, ec)) return fs::weakly_canonical(p, ec).string();
	return {};
}

} // namespace chimera::vm
