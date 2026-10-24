/* The program on Windows: Chimera's Qt window lives in lib\chimera-qt-app.dll,
 * with every DLL it needs, Qt's plugins and QML beside it, and the tools in
 * bin\, so the program's folder holds only this file and what the person
 * keeps there. Windows looks for a program's DLLs beside the program; this
 * tells it to look in lib\ as well, and puts lib\ on PATH for the tools the
 * window starts (chimera-run, ffmpeg). */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

int main(int argc, char **argv)
{
	wchar_t dir[MAX_PATH], lib[MAX_PATH], app[MAX_PATH];
	DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return 1;
	wchar_t *slash = wcsrchr(dir, L'\\');
	if (slash != NULL) *slash = L'\0';
	_snwprintf(lib, MAX_PATH, L"%ls\\lib", dir);
	_snwprintf(app, MAX_PATH, L"%ls\\chimera-qt-app.dll", lib);
	lib[MAX_PATH - 1] = app[MAX_PATH - 1] = L'\0';

	/* the tools the window starts find their DLLs on PATH */
	DWORD had = GetEnvironmentVariableW(L"PATH", NULL, 0);
	wchar_t *path = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (had + MAX_PATH + 2) * sizeof(wchar_t));
	if (path != NULL)
	{
		wcscpy(path, lib);
		if (had > 0)
		{
			wcscat(path, L";");
			GetEnvironmentVariableW(L"PATH", path + wcslen(path), had);
		}
		SetEnvironmentVariableW(L"PATH", path);
		HeapFree(GetProcessHeap(), 0, path);
	}

	/* this process's DLLs: lib\, then Windows' own */
	SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	AddDllDirectory(lib);
	HMODULE window = LoadLibraryExW(app, NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
	if (window == NULL)
	{
		DWORD code = GetLastError();
		wchar_t text[MAX_PATH + 160];
		_snwprintf(text, MAX_PATH + 160, L"Chimera could not load %ls (error %lu).\nIs the lib folder beside chimera-qt.exe?", app, code);
		text[MAX_PATH + 159] = L'\0';
		fwprintf(stderr, L"%ls\n", text);
		MessageBoxW(NULL, text, L"Chimera", MB_ICONERROR);
		return 1;
	}
	typedef int (*Main)(int, char **);
	Main run = (Main)(void *)GetProcAddress(window, "chimera_qt_main");
	if (run == NULL)
	{
		fwprintf(stderr, L"%ls has no chimera_qt_main\n", app);
		return 1;
	}
	return run(argc, argv);
}
