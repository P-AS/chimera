#include "chimera/vm/comm_vm.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace chimera::vm {

struct CommViewModel::Mapping
{
#ifdef _WIN32
	HANDLE handle = nullptr;
#else
	int fd = -1;
#endif
	int64_t size = 0;
};

namespace {
#ifdef _WIN32
std::wstring wide(const std::string &s)
{
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}
#else
std::string shmName(const std::string &name) { return "/" + name; }
#endif
} // namespace

CommViewModel::CommViewModel(Dispatcher &dispatcher, std::function<std::string()> screenshot)
	: ViewModel("comm", dispatcher), screenshot_(std::move(screenshot))
{
	expose("screenshotFile", "the memory-mapped file mmfScreenshot writes to", screenshotFile, true);
	command("mmfWrite", "bytes written to a memory-mapped file; how many", {"name", "bytes"}, [this](const Args &a, std::string &error) -> Value {
		std::string name, bytes;
		if (!arg(a, 0, name, error) || !arg(a, 1, bytes, error)) return {};
		const int64_t n = write(name, bytes, error);
		if (n < 0) return {};
		return n;
	});
	command("mmfRead", "bytes read from a memory-mapped file that is there", {"name", "size"}, [this](const Args &a, std::string &error) -> Value {
		std::string name, out;
		int64_t size = 0;
		if (!arg(a, 0, name, error) || !arg(a, 1, size, error)) return {};
		if (!read(name, size, out, error)) return {};
		return out;
	});
	command("mmfScreenshot", "the picture on screen, as a PNG, written to the screenshot file; how many bytes", {},
		[this](const Args &, std::string &error) -> Value {
			if (screenshotFile.get().empty()) return int64_t{0};
			const std::string png = screenshot_ ? screenshot_() : std::string();
			if (png.empty())
			{
				error = "there is no picture yet";
				return {};
			}
			const int64_t n = write(screenshotFile.get(), png, error);
			if (n < 0) return {};
			return n;
		});
}

CommViewModel::~CommViewModel()
{
	for (auto &[name, m] : open_)
	{
#ifdef _WIN32
		if (m->handle != nullptr) CloseHandle(m->handle);
#else
		if (m->fd >= 0) ::close(m->fd);
#endif
		delete m;
	}
}

int64_t CommViewModel::write(const std::string &name, const std::string &bytes, std::string &error)
{
	if (name.empty())
	{
		error = "a memory-mapped file needs a name";
		return -1;
	}
	const int64_t n = static_cast<int64_t>(bytes.size());
	Mapping *&m = open_[name];
	if (m == nullptr) m = new Mapping;
#ifdef _WIN32
	/* made, or the one there opened; one too small for this made again, as Chimera does */
	if (m->handle == nullptr || m->size < n)
	{
		if (m->handle != nullptr) CloseHandle(m->handle);
		m->handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(static_cast<uint64_t>(n) >> 32),
			static_cast<DWORD>(n & 0xFFFFFFFF), wide(name).c_str());
		if (m->handle == nullptr)
		{
			error = "could not make the memory-mapped file " + name;
			return -1;
		}
		m->size = n;
	}
	if (n == 0) return 0;
	void *view = MapViewOfFile(m->handle, FILE_MAP_WRITE, 0, 0, static_cast<SIZE_T>(n));
	if (view == nullptr)
	{
		error = "could not write the memory-mapped file " + name;
		return -1;
	}
	std::memcpy(view, bytes.data(), bytes.size());
	UnmapViewOfFile(view);
#else
	if (m->fd < 0)
	{
		m->fd = shm_open(shmName(name).c_str(), O_RDWR | O_CREAT, 0600);
		if (m->fd < 0)
		{
			error = "could not make the memory-mapped file " + name + ": " + std::strerror(errno);
			return -1;
		}
	}
	struct stat st{};
	fstat(m->fd, &st);
	if (st.st_size < n && ftruncate(m->fd, n) != 0)
	{
		error = "could not grow the memory-mapped file " + name + ": " + std::strerror(errno);
		return -1;
	}
	m->size = std::max<int64_t>(st.st_size, n);
	if (n == 0) return 0;
	void *view = mmap(nullptr, static_cast<size_t>(n), PROT_WRITE, MAP_SHARED, m->fd, 0);
	if (view == MAP_FAILED)
	{
		error = "could not write the memory-mapped file " + name + ": " + std::strerror(errno);
		return -1;
	}
	std::memcpy(view, bytes.data(), bytes.size());
	munmap(view, static_cast<size_t>(n));
#endif
	return n;
}

bool CommViewModel::read(const std::string &name, int64_t size, std::string &out, std::string &error)
{
	if (size < 0)
	{
		error = "no size below 0";
		return false;
	}
	out.assign(static_cast<size_t>(size), '\0');
	if (size == 0) return true;
#ifdef _WIN32
	HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, wide(name).c_str());
	if (h == nullptr)
	{
		error = "there is no memory-mapped file " + name;
		return false;
	}
	const void *view = MapViewOfFile(h, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(size));
	if (view == nullptr)
	{
		CloseHandle(h);
		error = "the memory-mapped file " + name + " is smaller than " + std::to_string(size) + " bytes";
		return false;
	}
	std::memcpy(out.data(), view, out.size());
	UnmapViewOfFile(view);
	CloseHandle(h);
#else
	const int fd = shm_open(shmName(name).c_str(), O_RDONLY, 0);
	if (fd < 0)
	{
		error = "there is no memory-mapped file " + name;
		return false;
	}
	struct stat st{};
	fstat(fd, &st);
	if (st.st_size < size)
	{
		::close(fd);
		error = "the memory-mapped file " + name + " is smaller than " + std::to_string(size) + " bytes";
		return false;
	}
	const void *view = mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_SHARED, fd, 0);
	::close(fd);
	if (view == MAP_FAILED)
	{
		error = "could not read the memory-mapped file " + name;
		return false;
	}
	std::memcpy(out.data(), view, out.size());
	munmap(const_cast<void *>(view), static_cast<size_t>(size));
#endif
	return true;
}

} // namespace chimera::vm
