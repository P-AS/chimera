/* Chimera's Qt window, a prototype (issue 142): the main window over the
 * session and piano roll view models. Everything a window does here goes
 * through the view models' registries, so a script could do the same.
 *
 *   chimera-qt [<package> <game>] [--fill <frames>] [--play]
 *              [--measure <seconds>] [--scroll] [--screenshot <path>]
 *
 * --measure runs for that long and prints what it measured, then quits:
 * frames the machine ran, pictures drawn, the window's frame intervals and,
 * with --scroll, how long each piano roll paint took while it scrolled. */

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
#include <vector>

#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "key_names.hpp"
#include "picture_item.hpp"
#include "piano_roll_item.hpp"
#include "qml_view_model.hpp"

namespace {

using namespace chimera;

/* Posts work into Qt's event loop: a view model's thread-hopping, from any thread. */
class QtDispatcher : public vm::Dispatcher
{
public:
	explicit QtDispatcher(QObject *context) : context_(context) {}
	void post(std::function<void()> fn) override
	{
		QMetaObject::invokeMethod(context_, std::move(fn), Qt::QueuedConnection);
	}

private:
	QObject *context_;
};

/* Keys pressed in the window go to the machine, by Chimera's key names; a
 * window that loses the keyboard lets go of every key it held. */
class KeyFilter : public QObject
{
public:
	explicit KeyFilter(vm::SessionViewModel &session) : session_(session) {}

protected:
	bool eventFilter(QObject *watched, QEvent *e) override
	{
		if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease)
		{
			auto *k = static_cast<QKeyEvent *>(e);
			if (!k->isAutoRepeat())
			{
				const QString name = qt::keyName(k->key(), k->modifiers());
				if (!name.isEmpty())
				{
					const bool down = e->type() == QEvent::KeyPress;
					session_.key(name.toStdString(), down);
					if (down) down_.insert(name);
					else down_.erase(name);
				}
			}
		}
		else if (e->type() == QEvent::FocusOut || e->type() == QEvent::WindowDeactivate)
		{
			for (const QString &name : down_) session_.key(name.toStdString(), false);
			down_.clear();
		}
		return QObject::eventFilter(watched, e);
	}

private:
	vm::SessionViewModel &session_;
	std::set<QString> down_;
};

struct Spread
{
	double median = 0, p95 = 0, worst = 0;
};

Spread spread(std::vector<double> v)
{
	Spread s;
	if (v.empty()) return s;
	std::sort(v.begin(), v.end());
	s.median = v[v.size() / 2];
	s.p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)];
	s.worst = v.back();
	return s;
}

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	QGuiApplication::setApplicationName(QStringLiteral("Chimera"));
	QQuickStyle::setStyle(QStringLiteral("Fusion"));

	QCommandLineParser args;
	args.addHelpOption();
	args.addPositionalArgument(QStringLiteral("package"), QStringLiteral("a core package to open"));
	args.addPositionalArgument(QStringLiteral("game"), QStringLiteral("the game file to open it with"));
	QCommandLineOption fillOpt(QStringLiteral("fill"), QStringLiteral("make the inputs this many frames long"), QStringLiteral("frames"));
	QCommandLineOption playOpt(QStringLiteral("play"), QStringLiteral("start running once open"));
	QCommandLineOption measureOpt(QStringLiteral("measure"), QStringLiteral("measure for this many seconds, print, quit"), QStringLiteral("seconds"));
	QCommandLineOption scrollOpt(QStringLiteral("scroll"), QStringLiteral("while measuring, scroll the piano roll all the time"));
	QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("when measuring ends, save the window as this picture"), QStringLiteral("path"));
	args.addOptions({fillOpt, playOpt, measureOpt, scrollOpt, shotOpt});
	args.process(app);

	QtDispatcher dispatcher(&app);
	vm::InputLog inputs;
	vm::SessionViewModel session(dispatcher, inputs);
	vm::PianoRollViewModel pianoRoll(dispatcher, inputs, session);
	qt::QmlViewModel sessionQml(session);
	qt::QmlViewModel pianoRollQml(pianoRoll);

	qmlRegisterType<qt::PictureItem>("Chimera", 1, 0, "PictureItem");
	qmlRegisterType<qt::PianoRollItem>("Chimera", 1, 0, "PianoRollItem");

	QQmlApplicationEngine engine;
	engine.setInitialProperties({
		{QStringLiteral("session"), QVariant::fromValue<QObject *>(&sessionQml)},
		{QStringLiteral("pianoRoll"), QVariant::fromValue<QObject *>(&pianoRollQml)},
	});
	engine.load(QUrl(QStringLiteral("qrc:/chimera/Main.qml")));
	if (engine.rootObjects().isEmpty()) return 1;
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
	KeyFilter keys(session);
	window->installEventFilter(&keys);

	std::string error;
	if (args.isSet(fillOpt)) pianoRoll.run("fill", {static_cast<int64_t>(args.value(fillOpt).toLongLong())}, error);
	const QStringList positional = args.positionalArguments();
	if (positional.size() >= 2)
	{
		session.run("open", {positional[0].toStdString(), positional[1].toStdString()}, error);
		if (args.isSet(playOpt))
		{
			/* play once the machine is open */
			auto connection = std::make_shared<vm::Connection>();
			*connection = session.isOpen.changed.connect([&session, connection] {
				if (!session.isOpen.get()) return;
				std::string e;
				session.run("play", {}, e);
				connection->disconnect();
			});
		}
	}

	/* measuring */
	std::mutex swapM;
	std::vector<qint64> swaps;
	QElapsedTimer clock;
	clock.start();
	std::vector<double> paints;
	auto *roll = window->findChild<qt::PianoRollItem *>(QStringLiteral("pianoRoll"));
	auto *picture = window->findChild<qt::PictureItem *>(QStringLiteral("picture"));
	int64_t framesAtStart = -1;
	qint64 shownAtStart = 0;
	QElapsedTimer runClock;
	vm::Connection startWatch;
	if (args.isSet(measureOpt))
	{
		const double seconds = args.value(measureOpt).toDouble();
		QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] {
			std::lock_guard<std::mutex> lock(swapM);
			swaps.push_back(clock.nsecsElapsed());
		}, Qt::DirectConnection);
		if (roll != nullptr)
			QObject::connect(roll, &qt::PianoRollItem::painted, roll, [&] { paints.push_back(roll->lastPaintMs()); });
		auto *scroller = new QTimer(&app);
		if (args.isSet(scrollOpt) && roll != nullptr)
		{
			pianoRoll.followPlayback.set(false);
			QObject::connect(scroller, &QTimer::timeout, &app, [&] {
				const int64_t count = std::max<int64_t>(1, inputs.size());
				pianoRoll.topFrame.set((pianoRoll.topFrame.get() + 997) % count);
			});
			scroller->start(16);
		}
		/* the clock starts when the machine does */
		startWatch = session.running.changed.connect([&] {
			if (session.running.get() && framesAtStart < 0)
			{
				framesAtStart = session.frame.get();
				shownAtStart = picture != nullptr ? picture->framesShown() : 0;
				runClock.start();
			}
		});
		QTimer::singleShot(static_cast<int>(seconds * 1000), &app, [&] {
			std::vector<double> intervals;
			{
				std::lock_guard<std::mutex> lock(swapM);
				for (size_t i = 1; i < swaps.size(); i++) intervals.push_back((swaps[i] - swaps[i - 1]) / 1.0e6);
			}
			const double ran = runClock.isValid() ? runClock.nsecsElapsed() / 1.0e9 : 0;
			const int64_t frames = framesAtStart >= 0 ? session.frame.get() - framesAtStart : 0;
			const qint64 shown = picture != nullptr ? picture->framesShown() - shownAtStart : 0;
			const Spread w = spread(intervals), p = spread(paints);
			std::printf("status: %s\n", session.status.get().c_str());
			std::printf("machine: %lld frames in %.2f s = %.2f fps (the machine's own rate is its vsync)\n",
				static_cast<long long>(frames), ran, ran > 0 ? frames / ran : 0.0);
			std::printf("pictures drawn: %lld\n", static_cast<long long>(shown));
			std::printf("window frames: %zu, interval ms median %.2f, 95th %.2f, worst %.2f\n",
				intervals.size() + (swaps.empty() ? 0 : 1), w.median, w.p95, w.worst);
			std::printf("piano roll: %lld rows, %zu paints, ms median %.3f, 95th %.3f, worst %.3f\n",
				static_cast<long long>(inputs.size()), paints.size(), p.median, p.p95, p.worst);
			if (args.isSet(shotOpt))
				std::printf("screenshot: %s\n", window->grabWindow().save(args.value(shotOpt)) ? "saved" : "FAILED");
			std::fflush(stdout);
			app.quit();
		});
	}

	return app.exec();
}
