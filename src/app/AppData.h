// The app data folder: chosen by the user at first run (or "don't keep app data": a temporary folder deleted on
// close). The choice itself is remembered where the app can find it before anything else is opened:
// Windows registry HKCU\Software\<org>\<app>\DataFolder, elsewhere ~/.config/<app>/data_folder.
#pragma once

#include <QString>

class QWidget;

namespace appdata {

QString suggestedFolder();
QString rememberedChoice();                 // empty if never chosen; "<none>" for "don't keep"
void rememberChoice(const QString& folder);
// The settings file of a folder already chosen (--data-folder or the remembered choice), before resolve() runs:
// the language is read from it before the first-run window shows. Empty if no folder has been chosen yet.
QString knownSettingsFile(const QString& override);
// Resolve the folder to use: --data-folder override, the remembered choice, or ask (first run).
// Returns an empty string if the user cancelled the first-run dialog.
QString resolve(const QString& override, QWidget* parent);
bool isTemporary();
bool usesOverride();                        // this run's folder came from --data-folder
QString settingsFile();                     // <folder>/settings.json
QString folder();
// File > Settings > Change app data folder: ask, move settings.json, remember. False if cancelled.
bool change(QWidget* parent, QString* message);

}  // namespace appdata
