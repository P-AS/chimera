#include "chimera/vm/batch_run_vm.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "chimera/engine.h"

namespace chimera::vm {

BatchRunViewModel::BatchRunViewModel(Dispatcher &dispatcher, RunTool run, ExtensionFinder byExtension)
	: ViewModel("batchRun", dispatcher), run_(std::move(run)), byExtension_(std::move(byExtension))
{
	expose("frames", "frames to run each game", frames, true);
	expose("done", "games run so far", done);
	expose("total", "games to run", total);
	expose("fileCount", "games in the list", fileCount);
	expose("running", "the games are being run", running);
	expose("status", "where the run is: \"Status: Running...\", \"Status: Finished!\"", status);
	expose("workFolder", "a folder for the runner's files while it works", workFolder, true);
	rows("files", "the games to run, a path each", fileRows_);
	rows("results", "what came of each: file, core, status (Success, ErrorOnLoad, ExceptOnAdv), frames, lagFrames, sha1, detail",
		resultRows_);
	command("addFile", "a game to run", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string path;
		if (!arg(a, 0, path, error)) return {};
		files_.push_back(path);
		fileCount.set(static_cast<int64_t>(files_.size()));
		fileRows_.reset.emit();
		return static_cast<int64_t>(files_.size());
	});
	/* as Chimera's, the list can be cleared during a run: the run goes by
	 * the list it was started with */
	command("clear", "no games to run", {}, [this](const Args &, std::string &) -> Value {
		files_.clear();
		fileCount.set(0);
		fileRows_.reset.emit();
		return {};
	});
	command("go", "runs every game in turn", {}, [this](const Args &, std::string &error) -> Value {
		if (running.get())
		{
			error = "Old one still running!";
			return {};
		}
		if (files_.empty())
		{
			error = "No files!";
			return {};
		}
		/* 0 frames loads each game and runs none, as Chimera's does */
		if (frames.get() < 0)
		{
			error = "frames to run cannot be below 0";
			return {};
		}
		queue_ = files_;
		results_.clear();
		resultRows_.reset.emit();
		at_ = 0;
		cancel_ = false;
		done.set(0);
		total.set(static_cast<int64_t>(queue_.size()));
		running.set(true);
		status.set("Status: Running...");
		next();
		return {};
	});
	command("cancel", "stops after the game being run", {}, [this](const Args &, std::string &) -> Value {
		cancel_ = true;
		return {};
	});
	command("dump", "writes what came of each game, a line each, as Chimera's dump: file, file, core, status, frames, lag "
		"frames, SHA-1, board", {"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			if (results_.empty())
			{
				error = "No results to save!";
				return {};
			}
			std::ofstream out(path, std::ios::binary);
			for (const Result &r : results_)
				out << r.file << '\t' << r.file << '\t' << r.core << '\t' << r.status << '\t' << r.frames << '\t' << r.lagFrames << '\t'
					<< r.sha1 << '\t' << '\n';
			if (!out)
			{
				error = "could not write " + path;
				return {};
			}
			return static_cast<int64_t>(results_.size());
		});
}

void BatchRunViewModel::next()
{
	if (cancel_ || at_ >= queue_.size())
	{
		running.set(false);
		status.set(cancel_ ? "Status: Stopped" : "Status: Finished!");
		return;
	}
	const std::string file = queue_[at_];
	Result r;
	r.file = file;
	/* its SHA-1, as Chimera's dump writes the game's hash */
	char sha[41] = {};
	uint64_t length = 0;
	if (ce_sha1_file(file.c_str(), sha, &length) == 1) r.sha1 = sha;
	/* the installed core that takes its kind of file, the first when several do */
	std::string ext = std::filesystem::path(file).extension().string();
	if (!ext.empty()) ext.erase(0, 1);
	for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	const auto cores = byExtension_ ? byExtension_(ext) : std::vector<std::pair<std::string, std::string>>{};
	auto finish = [this](Result r) {
		results_.push_back(std::move(r));
		resultRows_.reset.emit();
		at_++;
		done.set(static_cast<int64_t>(at_));
		next();
	};
	if (cores.empty())
	{
		r.status = "ErrorOnLoad";
		r.detail = "no installed core takes ." + ext + " files";
		finish(std::move(r));
		return;
	}
	r.core = cores.front().first;
	/* the runner records frames of no input into a movie of its own, and
	 * says how many it ran and how many were lag frames */
	std::error_code ec;
	const std::filesystem::path work = workFolder.get().empty() ? std::filesystem::temp_directory_path(ec) : std::filesystem::path(workFolder.get());
	std::filesystem::create_directories(work, ec);
	const std::string tag = std::to_string(at_);
	const std::filesystem::path empty = work / ("batch-empty-" + tag + ".txt"), recorded = work / ("batch-recorded-" + tag + ".txt"),
								meta = work / ("batch-meta-" + tag + ".txt");
	{
		std::ofstream(empty, std::ios::binary).flush();
	}
	const int64_t n = frames.get();
	run_({cores.front().second, file, empty.string(), "--frames", std::to_string(n), "--record", recorded.string(), "--meta",
			 meta.string(), "--count-input-polls"},
		[this, r, n, empty, recorded, meta, finish](int code, std::string, std::string err) mutable {
			std::ifstream in(meta);
			std::string line;
			std::string said;
			while (std::getline(in, line))
			{
				const size_t eq = line.find('=');
				if (eq == std::string::npos) continue;
				const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
				if (key == "status") said = value;
				else if (key == "frames") r.frames = std::strtoll(value.c_str(), nullptr, 10);
				else if (key == "lagFrames") r.lagFrames = std::strtoll(value.c_str(), nullptr, 10);
				else if (key == "detail" && !value.empty()) r.detail = value;
			}
			in.close();
			std::error_code ec;
			std::filesystem::remove(empty, ec);
			std::filesystem::remove(recorded, ec);
			std::filesystem::remove(meta, ec);
			/* loaded and run through: Success; run, but stopped short: an
			 * exception on advance; never run: an error on load */
			if (said == "OK" && code == 0 && r.frames >= n)
				r.status = "Success";
			else if (said.empty() && r.frames == 0)
			{
				r.status = "ErrorOnLoad";
				if (r.detail.empty())
				{
					const size_t last = err.find_last_not_of("\n");
					std::string tail = last == std::string::npos ? err : err.substr(0, last + 1);
					if (const size_t nl = tail.find_last_of('\n'); nl != std::string::npos) tail = tail.substr(nl + 1);
					r.detail = tail.empty() ? "the runner exited with " + std::to_string(code) : tail;
				}
			}
			else
				r.status = "ExceptOnAdv";
			finish(std::move(r));
		});
}

Value BatchRunViewModel::FileRows::cell(int64_t row, int32_t) const
{
	if (row < 0 || row >= rowCount()) return {};
	return vm_.files_[static_cast<size_t>(row)];
}

std::string BatchRunViewModel::ResultRows::columnName(int32_t column) const
{
	static const char *const names[] = {"file", "core", "status", "frames", "lagFrames", "sha1", "detail"};
	return column >= 0 && column < 7 ? names[column] : "";
}

Value BatchRunViewModel::ResultRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Result &r = vm_.results_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return r.file;
	case 1: return r.core;
	case 2: return r.status;
	case 3: return r.frames;
	case 4: return r.lagFrames;
	case 5: return r.sha1;
	case 6: return r.detail;
	default: return {};
	}
}

} // namespace chimera::vm
