#pragma once
/* The window in a language (docs/translations.md). A language is a compiled
 * translation, chimera_<language>.qm, built into the program; English is
 * the words as written. Config > Language chooses one; at the first open it
 * is the first of the system's languages that has a translation, else
 * English (owner, 2026-10-28, issue 376). Only the window's own words are
 * translated: what the view models and the cores say stays as written. */

#include <QLocale>
#include <QString>
#include <QStringList>

class QCoreApplication;

namespace chimera::qt {

/* where the translations built into the program are */
inline const QString kTranslationsFolder = QStringLiteral(":/i18n");

/* the languages the folder has a translation for ("fr", "pt_BR"), sorted;
 * English is never among them: it needs none */
QStringList languagesIn(const QString &folder = kTranslationsFolder);

/* the language the window starts in: the one chosen, when the folder has it
 * ("en" always), else the first of the locale's languages the folder has,
 * the most precise name first (pt_BR before pt), else "en" */
QString startLanguage(const QString &chosen, const QString &folder = kTranslationsFolder, const QLocale &locale = QLocale());

/* the window's words in a language, in place of the one before ("en": the
 * words as written); false when the folder has no translation for it */
bool useLanguage(QCoreApplication &app, const QString &language, const QString &folder = kTranslationsFolder);

/* a language's name in that language ("francais" written as the French
 * write it); "English" for "en" */
QString languageName(const QString &language);

} // namespace chimera::qt
