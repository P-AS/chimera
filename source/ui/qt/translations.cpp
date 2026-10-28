#include "translations.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTranslator>

namespace chimera::qt {

namespace {
/* the translation installed now, none for English */
QTranslator *installed = nullptr;
} // namespace

QStringList languagesIn(const QString &folder)
{
	QStringList out;
	const QStringList files = QDir(folder).entryList({QStringLiteral("chimera_*.qm")}, QDir::Files, QDir::Name);
	for (const QString &f : files) out << QFileInfo(f).completeBaseName().mid(int(sizeof("chimera_") - 1));
	return out;
}

QString startLanguage(const QString &chosen, const QString &folder, const QLocale &locale)
{
	const QStringList there = languagesIn(folder);
	if (chosen == QLatin1String("en") || (!chosen.isEmpty() && there.contains(chosen))) return chosen;
	/* "fr-CA" is tried as fr_CA, then as fr */
	for (QString name : locale.uiLanguages())
	{
		name.replace(QLatin1Char('-'), QLatin1Char('_'));
		if (name.startsWith(QLatin1String("en"))) return QStringLiteral("en");
		if (there.contains(name)) return name;
		const QString general = name.section(QLatin1Char('_'), 0, 0);
		if (there.contains(general)) return general;
	}
	return QStringLiteral("en");
}

bool useLanguage(QCoreApplication &app, const QString &language, const QString &folder)
{
	auto *next = new QTranslator(&app);
	if (language != QLatin1String("en") && !next->load(QStringLiteral("chimera_") + language, folder))
	{
		delete next;
		return false;
	}
	if (installed != nullptr)
	{
		QCoreApplication::removeTranslator(installed);
		delete installed;
		installed = nullptr;
	}
	if (language == QLatin1String("en"))
	{
		delete next;
		return true;
	}
	QCoreApplication::installTranslator(next);
	installed = next;
	return true;
}

QString languageName(const QString &language)
{
	if (language == QLatin1String("en")) return QStringLiteral("English");
	const QLocale locale(language);
	QString name = locale.nativeLanguageName();
	if (name.isEmpty()) return language;
	name = name.left(1).toUpper() + name.mid(1);
	/* a language written for one country says which ("zh_CN": Simplified Chinese, China's) */
	if (language.contains(QLatin1Char('_'))) name += QStringLiteral(" (") + locale.nativeTerritoryName() + QLatin1Char(')');
	return name;
}

} // namespace chimera::qt
