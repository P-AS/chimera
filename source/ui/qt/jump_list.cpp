/* Chimera's jump list, on Windows: the taskbar's Recent
 * list shown for the program, and each project or game opened put on it as
 * a task that starts the program with that file. Elsewhere, nothing. */

#include "jump_list.hpp"

#ifdef _WIN32
#include <windows.h>

#include <objbase.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace chimera::qt {

#ifdef _WIN32
namespace {
/* one command line argument, quoted so that it comes back whole */
std::wstring quoted(const std::wstring &argument)
{
	if (!argument.empty() && argument.find_first_of(L" \"\t\n") == std::wstring::npos) return argument;
	std::wstring out = L"\"";
	size_t slashes = 0;
	for (wchar_t c : argument)
	{
		if (c == L'\\')
		{
			slashes++;
			continue;
		}
		/* backslashes before a quote are doubled, and the quote escaped */
		out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
		slashes = 0;
		out += c;
	}
	/* and those at the end, before the closing quote */
	out.append(slashes * 2, L'\\');
	return out + L"\"";
}

std::wstring wide(const std::string &utf8)
{
	if (utf8.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
	return out;
}
} // namespace

void JumpList::showRecent()
{
	/* COM on this thread, as Qt may already have it (S_FALSE then) */
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	ICustomDestinationList *list = nullptr;
	if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list)))) return;
	UINT slots = 0;
	IObjectArray *removed = nullptr;
	if (SUCCEEDED(list->BeginList(&slots, IID_PPV_ARGS(&removed))))
	{
		list->AppendKnownCategory(KDC_RECENT);
		list->CommitList();
		removed->Release();
	}
	list->Release();
}

void JumpList::addRecent(const std::string &path, const std::string &title)
{
	if (path.empty()) return;
	wchar_t exe[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return;
	IShellLinkW *link = nullptr;
	if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return;
	link->SetPath(exe);
	link->SetArguments(quoted(wide(path)).c_str());
	/* the entry's words: a link's title, as WPF's JumpTask sets it */
	IPropertyStore *props = nullptr;
	if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&props))))
	{
		PROPVARIANT value;
		if (SUCCEEDED(InitPropVariantFromString(wide(title).c_str(), &value)))
		{
			props->SetValue(PKEY_Title, value);
			props->Commit();
			PropVariantClear(&value);
		}
		props->Release();
	}
	SHAddToRecentDocs(SHARD_LINK, link);
	link->Release();
}
#else
void JumpList::showRecent() {}
void JumpList::addRecent(const std::string &, const std::string &) {}
#endif

} // namespace chimera::qt
