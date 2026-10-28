/* The window's texts in a language (docs/translations.md): the languages
 * in the languages folder beside the engine; the one the window starts in (the one chosen, else
 * at the first open the system's, else English); a language used and
 * another in its place, through the translator qsTrId asks. */
#include "../translations.hpp"

#include <QCoreApplication>
#include <QLocale>

#include <cstdio>

namespace {
int checks = 0, failed = 0;
void check(bool ok, const char *what, int line)
{
	checks++;
	if (!ok)
	{
		failed++;
		std::printf("FAIL %s:%d: %s\n", __FILE__, line, what);
	}
}
#define CHECK(x) check((x), #x, __LINE__)
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	namespace q = chimera::qt;
	/* the languages there, English first (owner, 2026-10-28) */
	const QStringList all = q::languages();
	CHECK(all.size() >= 8 && all.first() == QStringLiteral("en"));
	for (const char *code : {"de", "es", "fr", "it", "ja", "pt", "zh_CN"}) CHECK(all.contains(QString::fromLatin1(code)));
	/* nothing chosen yet (the first open): the system's languages in turn */
	const QLocale german(QLocale::German, QLocale::Germany), canadian(QLocale::French, QLocale::Canada);
	CHECK(q::startLanguage(QString(), german) == QStringLiteral("de"));
	CHECK(q::startLanguage(QString(), canadian) == QStringLiteral("fr"));
	CHECK(q::startLanguage(QString(), QLocale(QLocale::Chinese, QLocale::China)) == QStringLiteral("zh_CN"));
	CHECK(q::startLanguage(QString(), QLocale(QLocale::Portuguese, QLocale::Brazil)) == QStringLiteral("pt"));
	CHECK(q::startLanguage(QString(), QLocale(QLocale::English, QLocale::UnitedKingdom)) == QStringLiteral("en"));
	CHECK(q::startLanguage(QString(), QLocale(QLocale::Swahili)) == QStringLiteral("en"));
	/* chosen: kept, whatever the system speaks, while there is a table for it */
	CHECK(q::startLanguage(QStringLiteral("en"), canadian) == QStringLiteral("en"));
	CHECK(q::startLanguage(QStringLiteral("ja"), canadian) == QStringLiteral("ja"));
	CHECK(q::startLanguage(QStringLiteral("xx"), canadian) == QStringLiteral("fr"));
	/* used, and another in its place: what qsTrId answers */
	CHECK(q::useLanguage(app, QStringLiteral("en")));
	CHECK(qtTrId("common.file") == QStringLiteral("&File"));
	CHECK(q::useLanguage(app, QStringLiteral("fr")));
	CHECK(qtTrId("common.file") == QStringLiteral("&Fichier"));
	CHECK(!q::useLanguage(app, QStringLiteral("xx")));
	CHECK(qtTrId("common.file") == QStringLiteral("&Fichier"));
	CHECK(q::useLanguage(app, QStringLiteral("ja")));
	CHECK(qtTrId("main.language_menu") == QString::fromUtf8("\xe8\xa8\x80\xe8\xaa\x9e(&L) (Language)"));
	CHECK(q::useLanguage(app, QStringLiteral("en")));
	CHECK(qtTrId("main.language_menu") == QStringLiteral("&Language"));
	CHECK(q::languageName(QStringLiteral("en")) == QStringLiteral("English"));
	CHECK(q::languageName(QStringLiteral("fr")) == QString::fromUtf8("Fran\xc3\xa7" "ais"));
	CHECK(q::languageEnglishName(QStringLiteral("de")) == QStringLiteral("German"));
	std::printf("%d checks, %d failed\n", checks, failed);
	return failed == 0 ? 0 : 1;
}
