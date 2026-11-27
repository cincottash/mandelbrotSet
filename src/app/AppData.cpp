#include "AppData.h"
#include "core/Tr.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>

namespace appdata {

namespace {

const QString kNone = QStringLiteral("<none>");
QString g_folder;
QTemporaryDir* g_temp = nullptr;   // deleted (with its contents) when the app exits
bool g_override = false;

#ifndef Q_OS_WIN
QString choiceFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(QStringLiteral(APP_NAME "/data_folder"));
}
#endif

// Ask where to keep app data. Returns the folder, kNone, or empty if cancelled.
QString ask(QWidget* parent, const QString& current, bool firstRun)
{
    QDialog d(parent);
    d.setWindowTitle(firstRun ? T("Welcome to %1").arg(QStringLiteral(APP_TITLE)) : T("App Data Folder"));
    auto* lay = new QVBoxLayout(&d);
    auto* intro = new QLabel(T("Where should %1 keep its settings (theme, colors, saved places, the last view)?\n"
                               "Pictures and location files are saved wherever you choose when you save them, not here.")
                                 .arg(QStringLiteral(APP_TITLE)));
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto* row = new QHBoxLayout;
    auto* path = new QLineEdit(current.isEmpty() || current == kNone ? suggestedFolder() : current);
    auto* browse = new QPushButton(T("Browse…"));
    row->addWidget(path, 1);
    row->addWidget(browse);
    lay->addLayout(row);
    auto* none = new QCheckBox(T("Don't Keep App Data"));
    none->setToolTip(T("Start fresh every time; nothing is left behind."));
    none->setChecked(current == kNone);
    lay->addWidget(none);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(firstRun ? T("Continue") : T("Use This Folder"));
    lay->addWidget(buttons);
    QObject::connect(browse, &QPushButton::clicked, &d, [&] {
        const QString f = QFileDialog::getExistingDirectory(&d, T("Choose App Data Folder"), path->text());
        if (!f.isEmpty()) path->setText(QDir::toNativeSeparators(f));
    });
    QObject::connect(none, &QCheckBox::toggled, &d, [&](bool on) { path->setEnabled(!on); browse->setEnabled(!on); });
    path->setEnabled(!none->isChecked());
    browse->setEnabled(!none->isChecked());
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
        if (none->isChecked()) { d.accept(); return; }
        const QString p = path->text().trimmed();
        if (p.isEmpty()) return;
        if (!QDir(p).exists()) {
            if (QMessageBox::question(&d, T("Create Folder?"), T("This folder doesn't exist. Create it?\n\n%1").arg(p))
                != QMessageBox::Yes)
                return;
            if (!QDir().mkpath(p)) {
                QMessageBox::warning(&d, T("Couldn't Create Folder"), T("Couldn't create %1.").arg(p));
                return;
            }
        }
        d.accept();
    });
    d.resize(560, d.sizeHint().height());
    if (d.exec() != QDialog::Accepted) return {};
    return none->isChecked() ? kNone : QDir::cleanPath(path->text().trimmed());
}

bool useFolder(const QString& choice)
{
    if (choice == kNone) {
        delete g_temp;
        g_temp = new QTemporaryDir(QDir::temp().filePath(QStringLiteral(APP_NAME "-XXXXXX")));
        if (!g_temp->isValid()) return false;
        g_folder = g_temp->path();
        return true;
    }
    if (!QDir().mkpath(choice)) return false;
    delete g_temp;
    g_temp = nullptr;
    g_folder = choice;
    return true;
}

}  // namespace

QString suggestedFolder()
{
    // The system's folder for app data, then the app's name: %LOCALAPPDATA%\<App> on Windows, ~/.local/share/<App>
    // on Linux. (Qt's own app folder, AppLocalDataLocation, would add the publisher's name in between.)
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::toNativeSeparators(QDir(base).filePath(QStringLiteral(APP_NAME)));
}

QString rememberedChoice()
{
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\" APP_ORG "\\" APP_NAME), QSettings::NativeFormat);
    return reg.value(QStringLiteral("DataFolder")).toString();
#else
    QFile f(choiceFile());
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
#endif
}

void rememberChoice(const QString& folder)
{
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\" APP_ORG "\\" APP_NAME), QSettings::NativeFormat);
    reg.setValue(QStringLiteral("DataFolder"), folder);
#else
    QDir().mkpath(QFileInfo(choiceFile()).path());
    QFile f(choiceFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(folder.toUtf8());
#endif
}

QString knownSettingsFile(const QString& override)
{
    const QString choice = override.isEmpty() ? rememberedChoice() : override;
    if (choice.isEmpty() || choice == kNone || !QDir(choice).exists()) return {};
    return QDir(choice).filePath(QStringLiteral("settings.json"));
}

QString resolve(const QString& override, QWidget* parent)
{
    g_override = !override.isEmpty();
    if (!override.isEmpty()) return useFolder(override) ? g_folder : QString();
    QString choice = rememberedChoice();
    if (choice.isEmpty() || (choice != kNone && !QDir(choice).exists() && !QDir().mkpath(choice))) {
        choice = ask(parent, choice, true);
        if (choice.isEmpty()) return {};
        rememberChoice(choice);
    }
    return useFolder(choice) ? g_folder : QString();
}

bool isTemporary() { return g_temp != nullptr; }
bool usesOverride() { return g_override; }
QString folder() { return g_folder; }
QString settingsFile() { return QDir(g_folder).filePath(QStringLiteral("settings.json")); }

bool change(QWidget* parent, QString* message)
{
    const QString old = g_folder;
    const QString oldSettings = settingsFile();
    const QString choice = ask(parent, isTemporary() ? kNone : old, false);
    if (choice.isEmpty()) return false;
    if (!useFolder(choice)) {
        if (message) *message = T("Couldn't use %1.").arg(choice);
        return false;
    }
    g_override = false;
    rememberChoice(choice);
    if (QFile::exists(oldSettings) && !QFile::exists(settingsFile())) QFile::copy(oldSettings, settingsFile());
    if (message)
        *message = choice == kNone ? T("App data will no longer be kept after you close the app.")
                                   : T("App data folder is now %1.").arg(QDir::toNativeSeparators(g_folder));
    return true;
}

}  // namespace appdata
