// Theme table and the function that styles the whole app (or one widget, for hover previews) from it.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace themes {

// Color keys: bg card border text muted accent accent_hover accent_text input track log_bg
//             ok ok_bg warn warn_bg warn_border err err_bg
struct Theme {
    QString group;  // menu group, e.g. "Dark Themes"
    QString name;   // shown in the menu
    QString key;    // saved in settings
    QHash<QString, QString> colors;
    QString c(const QString& k) const { return colors.value(k); }
};

const QList<Theme>& all();
const Theme* find(const QString& key);
QStringList groups();          // menu order
bool systemIsDark();
const Theme& forChoice(const QString& key);  // "system" or unknown keys -> Light/Dark by the OS setting

// Style the whole application, or only `only` and its children (for previews).
void apply(const Theme& t, QWidget* only = nullptr);

}  // namespace themes
