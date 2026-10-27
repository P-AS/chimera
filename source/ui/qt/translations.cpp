#include "translations.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTranslator>

namespace chimera::qt {

QString loadTranslation(QCoreApplication &app, const QString &folder, const QLocale &locale)
{
	auto *translator = new QTranslator(&app);
	/* chimera_<language>.qm for each of the locale's languages in turn, the
	 * most precise name first (pt_BR before pt) */
	if (!translator->load(locale, QStringLiteral("chimera"), QStringLiteral("_"), folder))
	{
		delete translator;
		return {};
	}
	QCoreApplication::installTranslator(translator);
	const QString name = QFileInfo(translator->filePath()).completeBaseName();
	return name.mid(name.indexOf(QLatin1Char('_')) + 1);
}

} // namespace chimera::qt
