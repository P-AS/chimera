/* The window's words in the person's language (docs/translations.md): a
 * translation built for French is loaded for a French speaker, and the main
 * window's words come out in French; a language with no translation loads
 * nothing, and the words stay English. The French here is the test's own
 * (tests/chimera_fr.ts), built by the build's lrelease. */
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
	/* no French: the words as written */
	CHECK(QCoreApplication::translate("Main", "&File") == QStringLiteral("&File"));
	/* a German speaker: no translation, nothing loaded */
	CHECK(chimera::qt::loadTranslation(app, folder, QLocale(QLocale::German, QLocale::Germany)).isEmpty());
	CHECK(QCoreApplication::translate("Main", "&File") == QStringLiteral("&File"));
	/* a speaker of Canadian French: French's, the nearest there is */
	CHECK(chimera::qt::loadTranslation(app, folder, QLocale(QLocale::French, QLocale::Canada)) == QStringLiteral("fr"));
	CHECK(QCoreApplication::translate("Main", "&File") == QStringLiteral("&Fichier"));
	/* the folder built into the program has none yet */
	CHECK(chimera::qt::loadTranslation(app, chimera::qt::kTranslationsFolder, QLocale(QLocale::French)).isEmpty());
	std::printf("%d checks, %d failed\n", checks, failed);
	return failed == 0 ? 0 : 1;
}
