/* SPDX-License-Identifier: Apache-2.0 */
/* The language of the window: System (the computer's, when the Studio has it),
 * English or Arabic, chosen at the bottom of the sidebar ("ui/language") and
 * applied at the next start (a window rebuilt in another language half-way
 * would keep texts made before).
 *
 * The texts come from translations/evre_studio_<code>.ts, built into the
 * program (:/i18n/evre_studio_<code>.qm); English is the source, it has none. Qt's own texts (the OK and Cancel of its dialogs) come
 * from Qt's qtbase_<code>.qm. Arabic lays the window out right to left; the
 * chart, the bit view and the Monitor's frames stay left to right, and numbers
 * keep Western digits and a decimal point everywhere (no locale is set for
 * them). */
#pragma once

#include <QString>
#include <QStringList>

class QApplication;

namespace language {

/* the choices as saved: "system", "en", "ar" */
QStringList codes();
QString saved();
void save(const QString &code);
/* the language a choice gives here: "system" -> "ar" on an Arabic system, else "en" */
QString resolve(const QString &choice);
/* the translators and the direction for a language ("en" or "ar"); false: its file is missing (English then) */
bool apply(QApplication &app, const QString &code);
QString current(); /* the language applied ("en" until apply) */

} // namespace language
