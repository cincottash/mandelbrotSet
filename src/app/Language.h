// The app's language: English (the text in the code) or a translation from src/app/i18n/<code>.json.
// Asked at the first start, kept in the settings file, changed in File > Settings > Language (after a restart).
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class QDialog;
class QWidget;

namespace language {

struct Choice {
    QString code;   // "en", "es"
    QString name;   // in its own language: "English", "Español"
};

QList<Choice> available();
bool isKnown(const QString& code);
QString nameOf(const QString& code);

// The system's language if the app has it, otherwise English
QString systemDefault();

// Shows the app in this language from now on (text made earlier stays as it was). Unknown codes mean English.
void install(const QString& code);
QString current();

// The first-start question, in every language at once. Returns the chosen code; closing the window keeps the
// suggested one. Never shown in test, screenshot, --render or --record modes (the caller decides).
QString ask(QWidget* parent, const QString& suggested);
// The question's window, not yet shown, and the language ticked in it (tests look at it without showing it)
QDialog* makeAskDialog(QWidget* parent, const QString& suggested);
QString chosen(QDialog* d, const QString& suggested);

// Tests: every English text asked for since the last call that has no translation in the current language
QStringList takeMissing();

}  // namespace language
