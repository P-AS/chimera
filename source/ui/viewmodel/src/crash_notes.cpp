#include "chimera/vm/crash_notes.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace chimera::vm::crash {

namespace fs = std::filesystem;

namespace {

/* The block, laid out exactly as source/crash/chimera_crash.c reads it:
 * magic, version, the folder (UTF-16), the frame, the flight recorder's
 * address and size, the session's length and its bytes (UTF-8). */
constexpr uint32_t kMagic = 0x52434843u; /* "CHCR" */
constexpr uint32_t kVersion = 2;
constexpr size_t kFolderChars = 520;
constexpr size_t kFrameOffset = 8 + kFolderChars * 2;
constexpr size_t kGlRecorderOffset = kFrameOffset + 8;
constexpr size_t kGlRecorderBytesOffset = kGlRecorderOffset + 8;
constexpr size_t kSessionLengthOffset = kGlRecorderBytesOffset + 4;
constexpr size_t kSessionOffset = kSessionLengthOffset + 4;
constexpr size_t kSessionCapacity = 16380;
constexpr size_t kBlockSize = kSessionOffset + kSessionCapacity;

/* never freed: it is read when the process is already gone */
alignas(16) unsigned char block[kBlockSize];
std::atomic<bool> isArmed{false};
std::mutex sessionGate;

/* a path from UTF-8 text and back, whatever the system's code page */
fs::path pathOf(const std::string &utf8) { return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size())); }
std::string textOf(const fs::path &p)
{
	const std::u8string s = p.u8string();
	return std::string(reinterpret_cast<const char *>(s.data()), s.size());
}

void put32(size_t at, uint32_t v) { std::memcpy(block + at, &v, sizeof v); }
void put64(size_t at, uint64_t v) { std::memcpy(block + at, &v, sizeof v); }

#ifdef _WIN32
/* the folder written into the block, zero-filled past it: an old path may have been longer */
std::string writeFolder(const std::string &notesFolder)
{
	std::error_code ec;
	const fs::path root = fs::absolute(pathOf(notesFolder), ec).lexically_normal().make_preferred();
	const std::wstring w = root.wstring();
	if (ec || w.size() >= kFolderChars) return tr("crash.path_too_long", {notesFolder});
	fs::create_directories(root, ec);
	if (ec) return tr("crash.folder_not_made", {notesFolder});
	std::memset(block + 8, 0, kFolderChars * 2);
	std::memcpy(block + 8, w.data(), w.size() * sizeof(wchar_t));
	return "";
}

/* Where Windows allows a module to be a helper for a process that asks: a
 * per-user list whose values are module paths. Values naming this module
 * where it no longer is (a moved install, a deleted test copy) go; other
 * programs' helpers are theirs. */
const wchar_t *const kHelperModulesKey = L"Software\\Microsoft\\Windows\\Windows Error Reporting\\RuntimeExceptionHelperModules";

std::string allowModule(const std::wstring &module)
{
	HKEY key = nullptr;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, kHelperModulesKey, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
		return tr("crash.list_not_opened");
	std::vector<std::wstring> gone;
	for (DWORD i = 0;; i++)
	{
		wchar_t name[1024];
		DWORD length = 1024;
		if (RegEnumValueW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
		const fs::path p(name);
		std::wstring file = p.filename().wstring();
		std::transform(file.begin(), file.end(), file.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
		std::error_code ec;
		if (file == L"chimera_crash.dll" && !fs::exists(p, ec)) gone.push_back(name);
	}
	for (const std::wstring &g : gone) RegDeleteValueW(key, g.c_str());
	DWORD zero = 0;
	const bool listed = RegQueryValueExW(key, module.c_str(), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
	const bool ok = listed || RegSetValueExW(key, module.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE *>(&zero), sizeof zero) == ERROR_SUCCESS;
	RegCloseKey(key);
	return ok ? "" : tr("crash.not_allowed");
}
#endif

std::string lowered(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

bool startsWith(const std::string &s, const char *p) { return s.rfind(p, 0) == 0; }

} // namespace

std::string arm(const std::string &moduleFolder, const std::string &notesFolder)
{
#ifndef _WIN32
	(void)moduleFolder;
	(void)notesFolder;
	return tr("crash.by_wer");
#else
	if (isArmed.load()) return "";
	std::error_code ec;
	/* the path as Windows writes it, so the list of helpers holds one entry for one module */
	const fs::path module = fs::absolute(pathOf(moduleFolder) / kModuleFileName, ec).lexically_normal().make_preferred();
	if (ec || !fs::exists(module, ec)) return tr("crash.missing", {textOf(module)});
	std::memset(block, 0, kBlockSize);
	put32(0, kMagic);
	put32(4, kVersion);
	if (std::string why = writeFolder(notesFolder); !why.empty()) return why;
	if (std::string why = allowModule(module.wstring()); !why.empty()) return why;
	/* kernel32's, looked up: mingw's werapi.h does not compile on its own */
	using Register = HRESULT(WINAPI *)(PCWSTR, PVOID);
	const auto reg = reinterpret_cast<Register>(
		reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "WerRegisterRuntimeExceptionModule")));
	if (reg == nullptr) return tr("crash.no_helpers");
	if (const HRESULT hr = reg(module.wstring().c_str(), block); FAILED(hr))
	{
		char text[64];
		std::snprintf(text, sizeof text, "0x%08lX", static_cast<unsigned long>(hr));
		return tr("crash.refused", {text});
	}
	isArmed.store(true);
	prune(notesFolder, kNotesKept);
	return "";
#endif
}

bool armed() { return isArmed.load(); }

std::string repoint(const std::string &notesFolder)
{
	if (!isArmed.load()) return "";
#ifdef _WIN32
	return writeFolder(notesFolder);
#else
	(void)notesFolder;
	return "";
#endif
}

void noteFrame(int64_t frame)
{
	if (isArmed.load(std::memory_order_relaxed)) put64(kFrameOffset, static_cast<uint64_t>(frame));
}

void noteGlRecorder(const void *address, uint32_t bytes)
{
	if (!isArmed.load()) return;
	put32(kGlRecorderBytesOffset, 0);
	put64(kGlRecorderOffset, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(address)));
	put32(kGlRecorderBytesOffset, address == nullptr ? 0 : bytes);
}

void describeSession(const std::string &text)
{
	if (!isArmed.load()) return;
	const size_t length = std::min(text.size(), kSessionCapacity);
	std::lock_guard<std::mutex> lock(sessionGate);
	/* the length says 0 while the bytes change, so a crash between the two reads nothing half written */
	put32(kSessionLengthOffset, 0);
	std::memcpy(block + kSessionOffset, text.data(), length);
	put32(kSessionLengthOffset, static_cast<uint32_t>(length));
}

std::string Note::field(const std::string &name) const
{
	const auto it = fields.find(name);
	return it == fields.end() ? std::string() : it->second;
}

std::string Note::summary() const
{
	std::string kind = field("kind");
	if (const size_t cut = kind.find(" ("); cut != std::string::npos && cut > 0) kind = kind.substr(0, cut);
	if (kind.empty()) kind = tr("crash.exception", {field("code")});
	const std::string module = field("module");
	std::string where = module.empty() ? "" : " " + tr("crash.in_module", {module + "+" + field("offset")});
	if (const std::string who = whoseModule(module); !who.empty()) where += " (" + who + ")";
	const std::string frame = field("frame");
	const std::string at = frame.empty() || frame == "0" ? "" : " " + tr("crash.at_frame", {frame});
	return kind + where + at;
}

std::string whoseModule(const std::string &module)
{
	const std::string m = lowered(module);
	if (startsWith(m, "nvoglv") || startsWith(m, "nvwgf") || startsWith(m, "nvd3dum") || startsWith(m, "nvlddmkm"))
		return tr("crash.nvidia");
	if (startsWith(m, "atiogl") || startsWith(m, "atig6") || startsWith(m, "amdxx") || startsWith(m, "aticfx") || startsWith(m, "amdvlk"))
		return tr("crash.amd");
	if (startsWith(m, "ig") && (m.find("icd") != std::string::npos || m.find("xel") != std::string::npos ||
								   m.find("d3d") != std::string::npos || m.find("gl") != std::string::npos))
		return tr("crash.intel");
	if (startsWith(m, "vulkan-1") || m == "opengl32.dll") return tr("crash.graphics");
	return "";
}

bool parse(const std::string &text, const std::string &path, Note &out)
{
	std::string flat;
	flat.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++)
		if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')) flat += text[i];
	std::istringstream in(flat);
	std::string line;
	if (!std::getline(in, line) || line != "Chimera crash note") return false;
	Note n;
	n.path = path;
	std::string *section = nullptr;
	while (std::getline(in, line))
	{
		if (line.empty()) continue;
		if (line == "session:")
		{
			section = &n.session;
			continue;
		}
		if (startsWith(line, "stack") && line.back() == ':')
		{
			section = &n.stack;
			continue;
		}
		if (section != nullptr && startsWith(line, "  "))
		{
			*section += line.substr(2) + "\n";
			continue;
		}
		section = nullptr;
		if (const size_t eq = line.find('='); eq != std::string::npos && eq > 0) n.fields[line.substr(0, eq)] = line.substr(eq + 1);
	}
	n.time = n.field("time");
	const std::string pid = n.field("process");
	if (!pid.empty() && std::all_of(pid.begin(), pid.end(), [](char c) { return c >= '0' && c <= '9'; }))
		n.processId = std::stoll(pid);
	std::error_code ec;
	const fs::path dump = pathOf(path).replace_extension(".dmp");
	if (fs::is_regular_file(dump, ec)) n.dump = textOf(dump);
	out = std::move(n);
	return true;
}

namespace {
/* the notes' files, newest first (their names begin with the time) */
std::vector<fs::path> noteFiles(const std::string &folder)
{
	std::vector<fs::path> files;
	std::error_code ec;
	for (const auto &e : fs::directory_iterator(pathOf(folder), ec))
		if (e.path().extension() == ".txt") files.push_back(fs::path(e.path()).make_preferred());
	std::sort(files.begin(), files.end(), [](const fs::path &a, const fs::path &b) { return a.filename() > b.filename(); });
	return files;
}
} // namespace

std::vector<Note> notesIn(const std::string &folder)
{
	std::vector<Note> out;
	for (const fs::path &f : noteFiles(folder))
	{
		std::ifstream in(f, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		Note n;
		if (parse(text, textOf(f), n)) out.push_back(std::move(n));
	}
	return out;
}

std::optional<Note> noteFor(int64_t processId, const std::string &notBefore, const std::string &folder)
{
	if (processId <= 0 || notBefore.empty()) return std::nullopt;
	for (Note &n : notesIn(folder))
		if (n.processId == processId && !n.time.empty() && n.time >= notBefore) return std::move(n);
	return std::nullopt;
}

void prune(const std::string &folder, size_t keep)
{
	const std::vector<fs::path> files = noteFiles(folder);
	for (size_t i = keep; i < files.size(); i++)
	{
		std::error_code ec;
		fs::path dump = files[i];
		fs::remove(dump.replace_extension(".dmp"), ec);
		fs::remove(files[i], ec);
	}
}

std::string notBeforeStart(int64_t started)
{
#ifdef _WIN32
	if (started <= 20000000) return "";
	const uint64_t t = static_cast<uint64_t>(started) - 20000000; /* two seconds, in 100 ns */
	FILETIME utc{static_cast<DWORD>(t & 0xFFFFFFFFu), static_cast<DWORD>(t >> 32)};
	SYSTEMTIME u{}, local{};
	if (!FileTimeToSystemTime(&utc, &u) || !SystemTimeToTzSpecificLocalTime(nullptr, &u, &local)) return "";
	char text[32];
	std::snprintf(text, sizeof text, "%04u-%02u-%02u %02u:%02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute,
		local.wSecond);
	return text;
#else
	(void)started;
	return "";
#endif
}

} // namespace chimera::vm::crash
