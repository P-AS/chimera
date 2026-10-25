#pragma once
/* Chimera's single instance mode (Customize), on Windows only, as Chimera's:
 * the first window started holds a named mutex and listens on a named pipe;
 * a second one started finds the mutex taken, hands the files it was given
 * to the first through the pipe, and ends. The first opens the first file
 * handed over, as Chimera's opens a game double-clicked in Explorer. The
 * payload is Chimera's: each argument's UTF-8 bytes in hex, joined by '|'.
 * The names are the prototype's own, as its settings are, so that it and
 * Chimera, run side by side, never hand files to each other. Elsewhere,
 * nothing. */

#include <QStringList>

#include <functional>
#include <string>
#include <thread>

namespace chimera::qt {

class SingleInstance
{
public:
	SingleInstance() = default;
	SingleInstance(const SingleInstance &) = delete;
	SingleInstance &operator=(const SingleInstance &) = delete;
	~SingleInstance() { stop(); }

	/* whether the settings file asks for it (customize.singleInstance), read
	 * before anything else is made; always false off Windows */
	static bool wanted(const std::string &settingsPath);
	/* takes the mutex; when another window holds it, `arguments` are handed
	 * to it and true is returned: this one ends */
	bool forwardIfRunning(const QStringList &arguments);
	/* the first window listens: each set of arguments handed over is given
	 * to `take` on the window's thread */
	void listen(std::function<void(const QStringList &)> take);
	/* listening ends (before the window's things are gone) */
	void stop();

	/* Chimera's payload, and back */
	static std::string payload(const QStringList &arguments);
	static QStringList arguments(const std::string &payload);

private:
	void *mutex_ = nullptr, *stop_ = nullptr;
	std::thread listener_;
};

} // namespace chimera::qt
