#pragma once

#include <QObject>
#include <QString>

#include "chimera/vm/observable.hpp"

namespace chimera::vm {
class SessionViewModel;
}

namespace chimera::qt {

/* Chimera's --headless (its HeadlessMode), for a run nobody watches, such as
 * a core's tests: no window is shown, and what would wait for a person ends
 * the run instead, said on stderr in Chimera's words - a dialog (exit 64),
 * the core stopped (65, the work kept against a crash), a project whose
 * files were not found (64). Work a crash left behind is said, and the
 * project opens as it was last saved. */
class HeadlessRun : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool enabled READ enabled CONSTANT)
public:
	static constexpr int kDialog = 64;
	static constexpr int kCoreStopped = 65;

	HeadlessRun(bool enabled, vm::SessionViewModel &session, QObject *parent = nullptr);
	bool enabled() const { return enabled_; }
	/* --headless among the program's arguments, read before Qt starts */
	static bool askedFor(int argc, char **argv);
	/* before Qt starts: drawn off screen, so no display is needed */
	static void prepare();
	/* a dialog opened (the style's Dialog says so): the run ends, or goes on
	 * past one that has an answer for nobody */
	Q_INVOKABLE void dialogOpened(QObject *dialog);
	/* what would wait for a person, said, and the run ended (exit 64) */
	Q_INVOKABLE void blocked(const QString &caption, const QString &text);

private:
	void end(int code);

	bool enabled_;
	vm::SessionViewModel &session_;
	vm::Connection filesWatch_;
	bool ending_ = false;
};

} // namespace chimera::qt
