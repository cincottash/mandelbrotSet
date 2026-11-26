#include "Themes.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleHints>
#include <QWidget>

#include <algorithm>

namespace themes {

const Theme* find(const QString& key)
{
    for (const Theme& t : all())
        if (t.key == key) return &t;
    return nullptr;
}

QStringList groups()
{
    return {QStringLiteral("Fractal Themes"), QStringLiteral("Dark Themes"), QStringLiteral("Light Themes")};
}

bool systemIsDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

const Theme& forChoice(const QString& key)
{
    if (const Theme* t = find(key)) return *t;
    return *find(systemIsDark() ? QStringLiteral("dark") : QStringLiteral("light"));
}

static bool isLight(const QString& color)
{
    const QColor c(color);
    return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue() > 150;
}

static QString styleSheet(const Theme& t)
{
    const QString tick = isLight(t.c("accent_text")) ? QStringLiteral(":/check.svg") : QStringLiteral(":/check_dark.svg");
    QString s = QStringLiteral(R"(
QWidget { font-family: "Segoe UI"; font-size: 10pt; }
QMainWindow, QWidget#root { background: @bg; }
QMenuBar { background: @card; padding: 0; margin: 0; spacing: 0; }
QMenuBar::item { padding: 4px 8px; margin: 0; border-radius: 0; background: transparent; }
QMenuBar::item:selected { background: @track; }
QMenu { background: @card; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 26px; border-radius: 4px; color: @text; }
QMenu::item:selected { background: @accent; color: @accent_text; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QFrame#header, QFrame#bottomBar { background: @card; border: none; }
QFrame#header { border-bottom: 1px solid @border; }
QFrame#bottomBar { border-top: 1px solid @border; }
QFrame#card { background: @card; border: 1px solid @border; border-radius: 8px; }
QFrame#card QLabel, QFrame#card QCheckBox, QFrame#card QRadioButton { background: transparent; }
QLabel#appTitle { font-size: 13pt; font-weight: 600; }
QLabel#cardTitle { color: @muted; font-size: 8.5pt; font-weight: 700; letter-spacing: 0.6px; }
QLabel#muted { color: @muted; }
QLabel#value { font-weight: 600; }
QLineEdit, QSpinBox, QDateTimeEdit, QComboBox, QKeySequenceEdit QLineEdit {
    background: @input; border: 1px solid @border; border-radius: 6px; padding: 4px 8px; }
QLineEdit:focus, QSpinBox:focus, QDateTimeEdit:focus, QComboBox:focus { border: 1px solid @accent; }
QLineEdit:disabled, QSpinBox:disabled, QDateTimeEdit:disabled, QComboBox:disabled { color: @muted; }
/* Check boxes inside table cells, styled like QCheckBox's (otherwise they are bare ticks). */
QTableView::indicator { width: 14px; height: 14px; border: 1px solid @muted; border-radius: 3px; background: @input; }
QTableView::indicator:checked { background: @accent; border-color: @accent; image: url(@tick); }
/* Lists open below their box: Fusion puts the chosen item over the box, a drop up for the last item. */
QComboBox { combobox-popup: 0; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: url(@down); width: 12px; height: 12px; }
/* Date and time boxes (QDateEdit, QTimeEdit) are styled with the number boxes, arrows and buttons too. */
QSpinBox::up-arrow, QDateTimeEdit::up-arrow { image: url(@up); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDateTimeEdit::down-arrow { image: url(@down); width: 10px; height: 10px; }
QSpinBox::up-arrow:disabled, QSpinBox::down-arrow:disabled, QDateTimeEdit::up-arrow:disabled, QDateTimeEdit::down-arrow:disabled,
QComboBox::down-arrow:disabled { image: none; }
QRadioButton::indicator { width: 12px; height: 12px; border: 2px solid @muted; border-radius: 8px; background: @input; }
QRadioButton::indicator:hover { border-color: @accent; }
QRadioButton::indicator:checked { width: 6px; height: 6px; border: 5px solid @accent; background: @accent_text; }
QRadioButton::indicator:disabled { border-color: @border; background: @track; }
QComboBox QAbstractItemView { background: @card; border: 1px solid @border; selection-background-color: @accent;
    selection-color: @accent_text; }
QSpinBox::up-button, QSpinBox::down-button, QDateTimeEdit::up-button, QDateTimeEdit::down-button { width: 16px; border: none; }
QPushButton, QToolButton#tool { background: @card; border: 1px solid @border; border-radius: 6px; padding: 6px 14px; }
QPushButton:hover, QToolButton#tool:hover { border-color: @accent; }
QPushButton:disabled, QToolButton#tool:disabled { color: @muted; border-color: @border; }
QToolButton#tool::menu-indicator { subcontrol-position: right center; right: 6px; }
QToolButton#tool[popupMode="1"], QToolButton#tool[popupMode="2"] { padding-right: 22px; }
QPushButton#primary { background: @accent; color: @accent_text; border: none; font-weight: 600; padding: 7px 22px; }
QPushButton#primary:hover { background: @accent_hover; }
QPushButton#primary:disabled { background: @track; color: @muted; }
QPushButton#record { background: @err_bg; color: @err; border: 1px solid @err; font-weight: 600; padding: 7px 18px; }
QPushButton#record:disabled { background: @track; color: @muted; border-color: @border; }
QToolButton#link { border: none; color: @accent; padding: 2px 0; background: transparent; }
QToolButton#link:disabled { color: @muted; }
QPlainTextEdit { background: @log_bg; border: 1px solid @border; border-radius: 6px;
    font-family: "Cascadia Mono", Consolas, monospace; font-size: 9pt; padding: 4px; }
QTableView { background: @input; alternate-background-color: @log_bg; border: 1px solid @border; border-radius: 6px;
    gridline-color: @border; selection-background-color: @accent; selection-color: @accent_text; }
QTableView::item { padding: 2px 6px; }
QHeaderView::section { background: @card; color: @muted; border: none; border-bottom: 1px solid @border;
    padding: 5px 6px 5px 10px; font-weight: 600; }
QTableCornerButton::section { background: @card; border: none; }
QLabel#pill { border-radius: 11px; padding: 3px 12px; font-weight: 600; }
QLabel#pill[tone="ok"] { color: @ok; background: @ok_bg; }
QLabel#pill[tone="warn"] { color: @warn; background: @warn_bg; }
QLabel#pill[tone="err"] { color: @err; background: @err_bg; }
QLabel#pill[tone="idle"] { color: @muted; background: @track; }
QLabel[tone="err"] { color: @err; }
QLabel[tone="warn"] { color: @warn; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @muted; border-radius: 4px; background: @input; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(@tick); }
QCheckBox::indicator:disabled { background: @track; border-color: @border; }
QCheckBox::indicator:checked:disabled { background: @muted; border-color: @muted; }
QToolTip { background: @card; color: @text; border: 1px solid @border; padding: 4px; }
/* Thin rounded scroll bars without arrow buttons. */
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:vertical { background: @border; border-radius: 5px; min-height: 32px; margin: 1px; }
QScrollBar::handle:horizontal { background: @border; border-radius: 5px; min-width: 32px; margin: 1px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)");
    // Longest keys first, so "@accent" doesn't eat the start of "@accent_text".
    QStringList keys = t.colors.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const QString& k : keys) s.replace(QLatin1Char('@') + k, t.colors.value(k));
    s.replace(QStringLiteral("@tick"), tick);
    const bool darkTheme = !isLight(t.c("bg"));
    s.replace(QStringLiteral("@down"), darkTheme ? QStringLiteral(":/down_light.svg") : QStringLiteral(":/down_dark.svg"));
    s.replace(QStringLiteral("@up"), darkTheme ? QStringLiteral(":/up_light.svg") : QStringLiteral(":/up_dark.svg"));
    return s;
}

void apply(const Theme& t, QWidget* only)
{
    QPalette pal;
    const std::pair<QPalette::ColorRole, const char*> roles[] = {
        {QPalette::Window, "bg"},          {QPalette::Base, "input"},          {QPalette::AlternateBase, "log_bg"},
        {QPalette::Text, "text"},          {QPalette::WindowText, "text"},     {QPalette::Button, "card"},
        {QPalette::ButtonText, "text"},    {QPalette::Highlight, "accent"},    {QPalette::HighlightedText, "accent_text"},
        {QPalette::ToolTipBase, "card"},   {QPalette::ToolTipText, "text"},    {QPalette::PlaceholderText, "muted"},
        {QPalette::Mid, "border"},         {QPalette::Link, "accent"},         {QPalette::LinkVisited, "accent"},
    };
    for (const auto& [role, key] : roles) pal.setColor(role, QColor(t.c(QString::fromLatin1(key))));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, QColor(t.c(QStringLiteral("muted"))));

    if (only) {
        only->setPalette(pal);
        only->setStyleSheet(styleSheet(t));
    } else {
        QApplication::setPalette(pal);
        qApp->setStyleSheet(styleSheet(t));
    }
}

}  // namespace themes
