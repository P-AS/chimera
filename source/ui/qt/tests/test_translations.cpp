/* The window's words in a language (docs/translations.md): the languages a
 * folder has; the one the window starts in (the one chosen, else at the
 * first open the system's, else English); a language used and another put
 * in its place, English the words as written. The French here is the
 * test's own (tests/chimera_fr.ts), built by the build's lrelease. */
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
	const QString folder = QStringLiteral(CHIMERA_TEST_QM_DIR);
	namespace q = chimera::qt;
	CHECK(q::languagesIn(folder) == QStringList{QStringLiteral("fr")});
	/* the languages built into the program (owner, 2026-10-28) */
	CHECK(q::languagesIn(q::kTranslationsFolder) == QStringList({QStringLiteral("de"), QStringLiteral("es"), QStringLiteral("fr"),
		QStringLiteral("it"), QStringLiteral("ja"), QStringLiteral("pt"), QStringLiteral("zh_CN")}));
	/* nothing chosen yet (the first open): the system's languages in turn */
	const QLocale german(QLocale::German, QLocale::Germany), canadian(QLocale::French, QLocale::Canada);
	CHECK(q::startLanguage(QString(), folder, german) == QStringLiteral("en"));
	CHECK(q::startLanguage(QString(), folder, canadian) == QStringLiteral("fr"));
	CHECK(q::startLanguage(QString(), q::kTranslationsFolder, canadian) == QStringLiteral("fr"));
	CHECK(q::startLanguage(QString(), q::kTranslationsFolder, QLocale(QLocale::Chinese, QLocale::China)) == QStringLiteral("zh_CN"));
	CHECK(q::startLanguage(QString(), q::kTranslationsFolder, QLocale(QLocale::Portuguese, QLocale::Brazil)) == QStringLiteral("pt"));
	CHECK(q::startLanguage(QString(), q::kTranslationsFolder, QLocale(QLocale::Chinese, QLocale::Taiwan)) == QStringLiteral("en"));
	CHECK(q::startLanguage(QString(), q::kTranslationsFolder, QLocale(QLocale::Korean)) == QStringLiteral("en"));
	/* chosen: kept, whatever the system speaks, while there is a translation for it */
	CHECK(q::startLanguage(QStringLiteral("en"), folder, canadian) == QStringLiteral("en"));
	CHECK(q::startLanguage(QStringLiteral("fr"), folder, german) == QStringLiteral("fr"));
	CHECK(q::startLanguage(QStringLiteral("ja"), folder, canadian) == QStringLiteral("fr"));
	/* used, and another in its place */
	CHECK(QCoreApplication::translate("Main", "&File") == QStringLiteral("&File"));
	CHECK(q::useLanguage(app, QStringLiteral("fr"), folder));
	CHECK(QCoreApplication::translate("Main", "&File") == QStringLiteral("&Fichier"));
	CHECK(!q::useLanguage(app, QStringLiteral("de"), folder));
	CHECK(QCoreApplication::translate("Main", "&File") == QStringLiteral("&Fichier"));
	CHECK(q::useLanguage(app, QStringLiteral("en"), folder));
	CHECK(QCoreApplication::translate("Main", "&File") == QStringLiteral("&File"));
	CHECK(q::languageName(QStringLiteral("en")) == QStringLiteral("English"));
	CHECK(q::languageName(QStringLiteral("fr")) == QString::fromUtf8("Fran\xc3\xa7" "ais"));
	/* the built-in ones translate the window's words */
	CHECK(q::useLanguage(app, QStringLiteral("es")));
	CHECK(QCoreApplication::translate("Main", "&File") == QStringLiteral("&Archivo"));
	CHECK(q::useLanguage(app, QStringLiteral("ja")));
	CHECK(QCoreApplication::translate("Main", "&Language") == QString::fromUtf8("\xe8\xa8\x80\xe8\xaa\x9e(&L)"));
	CHECK(q::useLanguage(app, QStringLiteral("en")));
	std::printf("%d checks, %d failed\n", checks, failed);
	return failed == 0 ? 0 : 1;
}
