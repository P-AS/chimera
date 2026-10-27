#pragma once
/* The window in the person's language (docs/translations.md). A language is
 * a compiled translation, chimera_<language>.qm, built into the program; the
 * first of the person's languages that has one is used, and English
 * otherwise. Only the window's own words are translated: what the view
 * models and the cores say stays as it is written. */

#include <QLocale>
#include <QString>

class QCoreApplication;

namespace chimera::qt {

/* where the translations built into the program are */
inline const QString kTranslationsFolder = QStringLiteral(":/i18n");

/* Installs the translation of the first of the locale's languages that the
 * folder has. The language loaded ("fr", "pt_BR"), "" for none. */
QString loadTranslation(QCoreApplication &app, const QString &folder = kTranslationsFolder, const QLocale &locale = QLocale());

} // namespace chimera::qt
