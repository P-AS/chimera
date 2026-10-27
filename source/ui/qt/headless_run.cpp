#include "headless_run.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include <QCoreApplication>
#include <QMetaObject>
#include <QQuickItem>
#include <QStringList>
#include <QThread>

#include "chimera/vm/session_vm.hpp"

namespace chimera::qt {

namespace {

/* what a dialog says: the words of everything shown in it, in order */
void wordsOf(const QQuickItem *item, QStringList &out)
{
	if (item == nullptr || !item->isVisible()) return;
	const QVariant text = item->property("text");
	if (text.typeId() == QMetaType::QString && !text.toString().trimmed().isEmpty()) out << text.toString().simplified();
	for (const QQuickItem *child : item->childItems()) wordsOf(child, out);
}

} // namespace

HeadlessRun::HeadlessRun(bool enabled, vm::SessionViewModel &session, QObject *parent)
	: QObject(parent), enabled_(enabled), session_(session)
{
	if (!enabled_) return;
	/* Locate Project Files would wait for a person: the files are named and the run ends, as Chimera's */
	filesWatch_ = session_.resolvingFiles.changed.connect([this] {
		if (!session_.resolvingFiles.get() || ending_) return;
		std::string missing, other;
		for (const vm::SessionViewModel::ProjectFile &f : session_.projectFiles())
		{
			std::string &list = f.status == 1 ? missing : other;
			if (f.status == 0) continue;
			list += (list.empty() ? "" : ", ") + f.name;
		}
		std::string text = "these files were not found beside it or where this machine last had them: " + missing;
		if (!other.empty()) text += (missing.empty() ? std::string() : "; ") + "these are not the files it recorded: " + other;
		blocked(QStringLiteral("Cannot open the project"), QString::fromStdString(text));
	});
	/* a project that cannot open (no core for it, its firmware missing...): why, and the run ends */
	failedWatch_ = session_.openFailures.changed.connect([this] {
		blocked(QStringLiteral("Cannot open the project"), QString::fromStdString(session_.status.get()));
	});
}

bool HeadlessRun::askedFor(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
		if (std::strcmp(argv[i], "--headless") == 0) return true;
	return false;
}

void HeadlessRun::prepare()
{
	if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
	if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) qputenv("QT_QUICK_BACKEND", "software");
}

void HeadlessRun::dialogOpened(QObject *dialog)
{
	if (!enabled_ || ending_ || dialog == nullptr) return;
	const QString name = dialog->objectName();
	if (name == QStringLiteral("coreStoppedDialog"))
	{
		/* the run ends the ordinary way, with the reason and the work kept, as Chimera's */
		std::fprintf(stderr, "[headless] The core stopped: %s\n", session_.stoppedReason.get().c_str());
		std::string error;
		session_.run("stoppedChoice", {std::string("keepWork")}, error);
		end(kCoreStopped);
		return;
	}
	if (name == QStringLiteral("recoveryDialog"))
	{
		/* nobody to choose: said, and the project opens as it was last saved; the copy stays */
		std::fprintf(stderr, "[recovery] unsaved work up to %s was rebuilt into %s; opening the project as it was last saved\n",
			session_.recoveredUpTo.get().c_str(), session_.recoveredCopy.get().c_str());
		QMetaObject::invokeMethod(dialog, "reject", Qt::QueuedConnection);
		return;
	}
	QStringList words;
	wordsOf(dialog->property("contentItem").value<QQuickItem *>(), words);
	blocked(dialog->property("title").toString(), words.join(QStringLiteral(" ")));
}

void HeadlessRun::blocked(const QString &caption, const QString &text)
{
	if (!enabled_ || ending_) return;
	std::fprintf(stderr, "[headless] a modal dialog would have blocked; exiting %d\n", kDialog);
	std::fprintf(stderr, "[headless] caption: %s\n", qPrintable(caption));
	std::fprintf(stderr, "[headless] text: %s\n", qPrintable(text));
	end(kDialog);
}

void HeadlessRun::end(int code)
{
	ending_ = true;
	endedWith_ = code;
	std::fflush(stderr);
	/* at once while the window's loop runs; asked before it has started, an
	 * exit would be lost, so it waits for it */
	if (QThread::currentThread()->loopLevel() > 0) QCoreApplication::exit(code);
	else QMetaObject::invokeMethod(QCoreApplication::instance(), [code] { QCoreApplication::exit(code); }, Qt::QueuedConnection);
}

} // namespace chimera::qt
