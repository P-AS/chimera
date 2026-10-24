#pragma once
/* A program run beside this one, fed through its standard input, its last
 * lines of standard error kept to say why it stopped: what the encode runs
 * ffmpeg with, as Chimera does. Linux and Windows. */

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chimera::vm {

class Subprocess
{
public:
	Subprocess() = default;
	Subprocess(const Subprocess &) = delete;
	Subprocess &operator=(const Subprocess &) = delete;
	/* still running: stopped */
	~Subprocess();

	/* set before start: the program's standard output joins its standard
	 * error, and each line of either is told here, on a thread of its own
	 * (a compile session's progress) */
	std::function<void(const std::string &)> onLine;
	/* program by its path, with these arguments; false with the reason */
	bool start(const std::string &program, const std::vector<std::string> &args, std::string &error);
	/* false once the program has stopped reading (it ended) */
	bool write(const void *data, size_t size);
	/* the input closed: the program sees its end */
	void closeInput();
	/* waits at most ms (-1: as long as it takes) for it to end: its exit
	 * code, or -1 while it still runs */
	int wait(int ms);
	void kill();
	/* its last lines of standard error */
	std::string errorTail() const;

private:
	void readErrors();
#ifdef _WIN32
	void *process_ = nullptr, *input_ = nullptr, *errors_ = nullptr;
#else
	int pid_ = -1, input_ = -1, errors_ = -1;
#endif
	int exitCode_ = -1;
	bool ended_ = false;
	std::thread reader_;
	mutable std::mutex m_;
	std::deque<std::string> tail_;
};

/* a command's words, as a shell splits them: by spaces, with double quotes
 * keeping spaces in and a backslash before a quote keeping the quote */
std::vector<std::string> splitCommandLine(const std::string &text);

} // namespace chimera::vm
