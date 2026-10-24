#include "chimera/vm/subprocess.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace chimera::vm {

namespace {
constexpr size_t kTailLines = 8;

#ifdef _WIN32
std::wstring wide(const std::string &s)
{
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}

/* one argument as the C runtime reads it back: quoted when it must be, the
 * backslashes before a quote doubled */
std::wstring quoted(const std::wstring &arg)
{
	if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
	std::wstring out = L"\"";
	size_t slashes = 0;
	for (wchar_t c : arg)
	{
		if (c == L'\\')
		{
			slashes++;
			continue;
		}
		if (c == L'"') out.append(slashes * 2 + 1, L'\\');
		else out.append(slashes, L'\\');
		slashes = 0;
		out += c;
	}
	out.append(slashes * 2, L'\\');
	out += L'"';
	return out;
}
#endif
} // namespace

std::vector<std::string> splitCommandLine(const std::string &text)
{
	std::vector<std::string> words;
	std::string word;
	bool inWord = false, inQuotes = false;
	for (size_t i = 0; i < text.size(); i++)
	{
		const char c = text[i];
		if (c == '\\' && i + 1 < text.size() && text[i + 1] == '"')
		{
			word += '"';
			inWord = true;
			i++;
		}
		else if (c == '"')
		{
			inQuotes = !inQuotes;
			inWord = true;
		}
		else if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && !inQuotes)
		{
			if (inWord) words.push_back(word);
			word.clear();
			inWord = false;
		}
		else
		{
			word += c;
			inWord = true;
		}
	}
	if (inWord) words.push_back(word);
	return words;
}

Subprocess::~Subprocess()
{
	closeInput();
	if (wait(0) < 0 && !ended_)
	{
		kill();
		wait(-1);
	}
	if (reader_.joinable()) reader_.join();
#ifdef _WIN32
	if (errors_ != nullptr) CloseHandle(errors_);
	if (process_ != nullptr) CloseHandle(process_);
#else
	if (errors_ >= 0) ::close(errors_);
#endif
}

std::string Subprocess::errorTail() const
{
	std::lock_guard<std::mutex> lock(m_);
	std::string out;
	for (const std::string &l : tail_) out += l + "\n";
	return out;
}

void Subprocess::readErrors()
{
	std::string line;
	char buf[4096];
	for (;;)
	{
#ifdef _WIN32
		DWORD got = 0;
		if (!ReadFile(errors_, buf, sizeof buf, &got, nullptr) || got == 0) break;
		const size_t n = got;
#else
		const ssize_t got = ::read(errors_, buf, sizeof buf);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) break;
		const size_t n = static_cast<size_t>(got);
#endif
		for (size_t i = 0; i < n; i++)
		{
			/* ffmpeg ends its progress lines with a carriage return */
			if (buf[i] == '\n' || buf[i] == '\r')
			{
				if (!line.empty())
				{
					if (onLine) onLine(line);
					std::lock_guard<std::mutex> lock(m_);
					tail_.push_back(line);
					if (tail_.size() > kTailLines) tail_.pop_front();
				}
				line.clear();
			}
			else
				line += buf[i];
		}
	}
	if (!line.empty())
	{
		if (onLine) onLine(line);
		std::lock_guard<std::mutex> lock(m_);
		tail_.push_back(line);
		if (tail_.size() > kTailLines) tail_.pop_front();
	}
}

#ifdef _WIN32

bool Subprocess::start(const std::string &program, const std::vector<std::string> &args, std::string &error)
{
	SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
	HANDLE inRead = nullptr, inWrite = nullptr, errRead = nullptr, errWrite = nullptr;
	if (!CreatePipe(&inRead, &inWrite, &sa, 1 << 20) || !CreatePipe(&errRead, &errWrite, &sa, 0))
	{
		error = "no pipe to the program";
		return false;
	}
	/* this side's ends are not the child's */
	SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
	HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
	std::wstring line = quoted(wide(program));
	for (const std::string &a : args) line += L" " + quoted(wide(a));
	STARTUPINFOW si{};
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = inRead;
	si.hStdOutput = onLine ? errWrite : nul;
	si.hStdError = errWrite;
	PROCESS_INFORMATION pi{};
	const BOOL made = CreateProcessW(wide(program).c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	CloseHandle(inRead);
	CloseHandle(errWrite);
	if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
	if (!made)
	{
		CloseHandle(inWrite);
		CloseHandle(errRead);
		error = "could not start " + program + " (error " + std::to_string(GetLastError()) + ")";
		return false;
	}
	CloseHandle(pi.hThread);
	process_ = pi.hProcess;
	input_ = inWrite;
	errors_ = errRead;
	reader_ = std::thread([this] { readErrors(); });
	return true;
}

bool Subprocess::write(const void *data, size_t size)
{
	if (input_ == nullptr) return false;
	const char *p = static_cast<const char *>(data);
	while (size > 0)
	{
		DWORD wrote = 0;
		const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 1u << 30));
		if (!WriteFile(input_, p, chunk, &wrote, nullptr)) return false;
		p += wrote;
		size -= wrote;
	}
	return true;
}

void Subprocess::closeInput()
{
	if (input_ != nullptr) CloseHandle(input_);
	input_ = nullptr;
}

int Subprocess::wait(int ms)
{
	if (ended_) return exitCode_;
	if (process_ == nullptr) return -1;
	if (WaitForSingleObject(process_, ms < 0 ? INFINITE : static_cast<DWORD>(ms)) != WAIT_OBJECT_0) return -1;
	DWORD code = 0;
	GetExitCodeProcess(process_, &code);
	exitCode_ = static_cast<int>(code);
	ended_ = true;
	if (reader_.joinable()) reader_.join();
	return exitCode_;
}

void Subprocess::kill()
{
	if (process_ != nullptr && !ended_) TerminateProcess(process_, 1);
}

#else

bool Subprocess::start(const std::string &program, const std::vector<std::string> &args, std::string &error)
{
	/* a program that stops reading must not take this one with it */
	std::signal(SIGPIPE, SIG_IGN);
	int in[2], err[2];
	if (pipe(in) != 0 || pipe(err) != 0)
	{
		error = "no pipe to the program";
		return false;
	}
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, in[0], 0);
	posix_spawn_file_actions_adddup2(&fa, err[1], 2);
	if (onLine)
		posix_spawn_file_actions_adddup2(&fa, err[1], 1);
	else
		posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addclose(&fa, in[1]);
	posix_spawn_file_actions_addclose(&fa, err[0]);
	std::vector<char *> argv;
	argv.push_back(const_cast<char *>(program.c_str()));
	for (const std::string &a : args) argv.push_back(const_cast<char *>(a.c_str()));
	argv.push_back(nullptr);
	pid_t pid = -1;
	const int rc = posix_spawn(&pid, program.c_str(), &fa, nullptr, argv.data(), environ);
	posix_spawn_file_actions_destroy(&fa);
	::close(in[0]);
	::close(err[1]);
	if (rc != 0)
	{
		::close(in[1]);
		::close(err[0]);
		error = "could not start " + program + ": " + std::strerror(rc);
		return false;
	}
	fcntl(in[1], F_SETFD, FD_CLOEXEC);
	fcntl(err[0], F_SETFD, FD_CLOEXEC);
	pid_ = pid;
	input_ = in[1];
	errors_ = err[0];
	reader_ = std::thread([this] { readErrors(); });
	return true;
}

bool Subprocess::write(const void *data, size_t size)
{
	if (input_ < 0) return false;
	const char *p = static_cast<const char *>(data);
	while (size > 0)
	{
		const ssize_t wrote = ::write(input_, p, size);
		if (wrote < 0 && errno == EINTR) continue;
		if (wrote <= 0) return false;
		p += wrote;
		size -= static_cast<size_t>(wrote);
	}
	return true;
}

void Subprocess::closeInput()
{
	if (input_ >= 0) ::close(input_);
	input_ = -1;
}

int Subprocess::wait(int ms)
{
	if (ended_) return exitCode_;
	if (pid_ < 0) return -1;
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms < 0 ? 0 : ms);
	for (;;)
	{
		int status = 0;
		const pid_t r = waitpid(pid_, &status, ms < 0 ? 0 : WNOHANG);
		if (r == pid_)
		{
			exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
			ended_ = true;
			if (reader_.joinable()) reader_.join();
			return exitCode_;
		}
		if (r < 0 && errno != EINTR) return -1;
		if (ms >= 0 && std::chrono::steady_clock::now() >= until) return -1;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void Subprocess::kill()
{
	if (pid_ > 0 && !ended_) ::kill(pid_, SIGKILL);
}

#endif

} // namespace chimera::vm
