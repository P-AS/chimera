#include "single_instance.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QMetaObject>

#include <cstdio>


#ifdef _WIN32
#include <windows.h>
#endif

namespace chimera::qt {

#ifdef _WIN32
namespace {
/* Chimera's names (MainForm.cs), and its hand-over: the arguments as UTF-8
 * hex joined by "|" (owner, 2026-10-26: the window takes Chimera's names) */
constexpr wchar_t kMutex[] = L"mutex-{84125ACB-F570-4458-9748-321F887FE795}";
constexpr wchar_t kPipe[] = L"\\\\.\\pipe\\pipe-{84125ACB-F570-4458-9748-321F887FE795}";

/* an overlapped call waited for: false when the stop came first (the call cancelled) */
bool waitFor(HANDLE pipe, OVERLAPPED &ov, HANDLE stop)
{
	HANDLE both[] = {ov.hEvent, stop};
	if (WaitForMultipleObjects(2, both, FALSE, INFINITE) == WAIT_OBJECT_0) return true;
	CancelIo(pipe);
	DWORD done = 0;
	GetOverlappedResult(pipe, &ov, &done, TRUE);
	return false;
}
} // namespace
#endif

std::string SingleInstance::payload(const QStringList &arguments)
{
	std::string out;
	for (qsizetype i = 0; i < arguments.size(); i++)
		out += (i == 0 ? "" : "|") + arguments[i].toUtf8().toHex().toUpper().toStdString();
	return out;
}

QStringList SingleInstance::arguments(const std::string &payload)
{
	QStringList out;
	for (const QByteArray &hex : QByteArray::fromStdString(payload).split('|')) out << QString::fromUtf8(QByteArray::fromHex(hex));
	return out;
}

bool SingleInstance::forwardIfRunning(const QStringList &arguments)
{
#ifdef _WIN32
	mutex_ = CreateMutexW(nullptr, TRUE, kMutex);
	/* no mutex at all: this one goes on, as the first */
	if (mutex_ == nullptr || GetLastError() != ERROR_ALREADY_EXISTS) return false;
	CloseHandle(mutex_);
	mutex_ = nullptr;
	/* the first one's pipe, waited for a little while it is between two
	 * callers (Chimera's waits not at all); this one ends either way */
	HANDLE pipe = INVALID_HANDLE_VALUE;
	for (int tries = 0; tries < 20 && pipe == INVALID_HANDLE_VALUE; tries++)
	{
		pipe = CreateFileW(kPipe, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE && !WaitNamedPipeW(kPipe, 100)) Sleep(100);
	}
	if (pipe == INVALID_HANDLE_VALUE)
	{
		std::fprintf(stderr, "Failed forwarding args to already-running single instance\n");
		return true;
	}
	const std::string bytes = payload(arguments);
	DWORD wrote = 0;
	if (!WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr) || wrote != bytes.size())
		std::fprintf(stderr, "Failed forwarding args to already-running single instance\n");
	CloseHandle(pipe);
	return true;
#else
	(void)arguments;
	return false;
#endif
}

void SingleInstance::listen(std::function<void(const QStringList &)> take)
{
#ifdef _WIN32
	if (mutex_ == nullptr || listener_.joinable()) return;
	stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	listener_ = std::thread([this, take = std::move(take)] {
		HANDLE stop = stop_;
		while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0)
		{
			/* one caller at a time, from this machine only */
			HANDLE pipe = CreateNamedPipeW(kPipe, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, 64 * 1024, 0, nullptr);
			if (pipe == INVALID_HANDLE_VALUE) return;
			OVERLAPPED ov{};
			ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			bool connected = ConnectNamedPipe(pipe, &ov) != 0;
			if (!connected)
			{
				const DWORD why = GetLastError();
				DWORD none = 0;
				connected = why == ERROR_PIPE_CONNECTED ||
					(why == ERROR_IO_PENDING && waitFor(pipe, ov, stop) && GetOverlappedResult(pipe, &ov, &none, FALSE) != 0);
			}
			/* the whole message, a piece at a time */
			std::string got;
			bool whole = false;
			while (connected)
			{
				char buf[4096];
				DWORD done = 0;
				ResetEvent(ov.hEvent);
				bool ok = ReadFile(pipe, buf, sizeof buf, nullptr, &ov) != 0;
				DWORD why = ok ? 0 : GetLastError();
				if (why == ERROR_IO_PENDING && !waitFor(pipe, ov, stop)) break;
				if (ok || why == ERROR_IO_PENDING || why == ERROR_MORE_DATA)
				{
					ok = GetOverlappedResult(pipe, &ov, &done, FALSE) != 0;
					why = ok ? 0 : GetLastError();
				}
				got.append(buf, done);
				whole = ok;
				if (why != ERROR_MORE_DATA) break;
			}
			CloseHandle(ov.hEvent);
			DisconnectNamedPipe(pipe);
			CloseHandle(pipe);
			if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return;
			if (!whole) continue;
			const QStringList handed = arguments(got);
			QMetaObject::invokeMethod(QCoreApplication::instance(), [take, handed] { take(handed); }, Qt::QueuedConnection);
		}
	});
#else
	(void)take;
#endif
}

void SingleInstance::stop()
{
#ifdef _WIN32
	if (listener_.joinable())
	{
		SetEvent(stop_);
		listener_.join();
	}
	if (stop_ != nullptr) CloseHandle(stop_);
	stop_ = nullptr;
	if (mutex_ != nullptr)
	{
		ReleaseMutex(mutex_);
		CloseHandle(mutex_);
	}
	mutex_ = nullptr;
#endif
}

} // namespace chimera::qt
