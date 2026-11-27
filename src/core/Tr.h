// Text the user sees goes through T(), so it shows in the language chosen at the first start (File > Settings >
// Language). The translations are in src/app/i18n/<code>.json; text without one shows in English.
#pragma once

#include <QCoreApplication>
#include <QString>

// English text the user sees (a label, tooltip, message or log line)
inline QString T(const char* english) { return QCoreApplication::translate("app", english); }

// The same, with a note for the translator where the same English needs different words in different places.
// The note is part of the key in the translation file.
inline QString T(const char* english, const char* note) { return QCoreApplication::translate("app", english, note); }

// Text kept in English in files and settings (a fractal's or palette's name) shown in the app's language
inline QString Tname(const QString& english) { return QCoreApplication::translate("app", english.toUtf8().constData()); }
