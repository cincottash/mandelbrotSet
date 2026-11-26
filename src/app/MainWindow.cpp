#include "MainWindow.h"

#include "AppData.h"
#include "FileDialogs.h"
#include "FractalView.h"
#include "Themes.h"
#include "Widgets.h"

#include "core/Minibrot.h"
#include "core/Palette.h"
#include "core/RandomScene.h"

#include <QPainter>
#include <QSignalBlocker>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>

namespace {

const QString kWebsite = QStringLiteral("https://github.com/cincottash/mandelbrotSet");

QString placesFile() { return QDir(appdata::folder()).filePath(QStringLiteral("places.json")); }

void setTone(QLabel* l, const char* tone)
{
    if (l->property("tone").toString() == QLatin1String(tone)) return;
    l->setProperty("tone", QString::fromLatin1(tone));
    l->style()->unpolish(l);
    l->style()->polish(l);
}

QString shortNumber(const core::BigFloat& v, int digits)
{
    return QString::fromStdString(v.toString(digits));
}

// Every card's grid: the same label column width and spacing, so labels, sliders and boxes line up everywhere.
QGridLayout* cardGrid()
{
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    grid->setColumnMinimumWidth(0, labelColumnWidth(QFontMetrics(QApplication::font())));
    return grid;
}

}  // namespace

MainWindow::MainWindow(Settings& s, bool interactive, QWidget* parent)
    : QMainWindow(parent), m_s(s), m_interactive(interactive)
{
    for (Qt::UIEffect e : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip, Qt::UI_FadeTooltip})
        m_effectsDefault.insert(int(e), QApplication::isEffectEnabled(e));
    setWindowTitle(QStringLiteral(APP_TITLE));
    m_lastMandelbrotView = core::View::home(false);

    m_themePreviewTimer.setSingleShot(true);
    m_themePreviewTimer.setInterval(90);
    connect(&m_themePreviewTimer, &QTimer::timeout, this, [this] {
        if (!m_themePreviewing.isEmpty()) themes::apply(themes::forChoice(m_themePreviewing), centralWidget());
    });
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveSettingsNow(); });
    m_editBurst.setSingleShot(true);
    m_editBurst.setInterval(700);
    connect(&m_editBurst, &QTimer::timeout, this, [this] { m_lastState = currentSettings(); });

    // The last session comes back as it was left.
    core::View startView = core::View::home(false);
    if (!m_s.session.isEmpty()) {
        core::Location l;
        QString err;
        if (l.fromJson(m_s.session, &err)) {
            m_fractal = l.fractal;
            if (l.hasColors) m_colors = l.colors;
            startView = l.view;
        }
    }

    buildMenus();
    buildUi();
    m_view->setFractal(m_fractal);
    m_view->setColors(m_colors);
    m_view->setView(startView);
    m_view->setAntiAliasing(m_s.antiAliasing);
    m_view->setZoomStep(std::pow(1.5, m_s.zoomSpeedPct / 100.0));
    m_view->setJuliaPreview(m_s.juliaPreview);
    syncControls();
    m_lastState = currentSettings();
    connect(m_view, &FractalView::pausedChanged, this, [this](bool on) {
        updateAnimationButtons();
        if (m_view->autopilot() || m_colors.cycleSpeed != 0.0)
            log(on ? QStringLiteral("Paused. Resume (or Space on the picture) goes on from here.") : QStringLiteral("Resumed."));
    });

    connect(m_view, &FractalView::viewSettled, this, [this] { rememberView(); saveSettingsSoon(); });
    connect(m_view, &FractalView::contextMenuRequested, this, &MainWindow::showViewMenu);
    connect(m_view, &FractalView::gpuUnavailable, this, [this](const QString& why) {
        m_gpuMessage->setText(QStringLiteral("The picture can't be drawn on this computer.\n\n%1").arg(why));
        m_gpuMessage->show();
        log(QStringLiteral("The graphics card renderer couldn't start: %1").arg(why));
        updateStatus();
    });
    connect(m_view, &FractalView::autopilotChanged, this, [this](bool on) {
        updateAnimationButtons();
        {
            QSignalBlocker block(m_autoAct);
            m_autoAct->setChecked(on);
        }
        log(on ? QStringLiteral("Autopilot on: zooming in, steering toward detail. Drag to steer, Escape to stop.")
               : QStringLiteral("Autopilot off at zoom %1×.").arg(m_view->view().magnificationText()));
        if (!on) rememberView();
        if (!m_statusTimer.isActive()) m_statusTimer.start();
    });
    connect(m_view, &FractalView::zoomLimitReached, this, [this](const QString& why) {
        static QString last;
        if (why != last) log(why);
        last = why;
    });

    loadPlaces();
    refreshPlaces();
    for (const QString& c : m_cards.keys()) setCardOpen(c, !m_s.foldedCards.contains(c), false);
    updateStyleVisibility();

    setMinimumSize(1000, 640);   // every button and the cards' columns fit from here up
    if (!m_s.geometry.isEmpty()) restoreGeometry(m_s.geometry);
    else if (QScreen* sc = QGuiApplication::primaryScreen()) {
        const QRect a = sc->availableGeometry();
        resize(std::min(int(a.width() * 0.88), 1720), std::min(int(a.height() * 0.88), 1060));
        move(a.center() - rect().center());
    }
    if (m_s.alwaysOnTop) setWindowFlag(Qt::WindowStaysOnTopHint, true);
    applyOpacity();
    qApp->installEventFilter(this);

    m_statusTimer.setInterval(200);
    connect(&m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer.start();
    connect(m_view, &FractalView::viewChanged, this, [this] {
        if (!m_statusTimer.isActive()) m_statusTimer.start();
    });

    log(QStringLiteral("Ready. Scroll to zoom, drag to move, right-drag to turn, right-click for more."));
    if (appdata::isTemporary()) log(QStringLiteral("Settings are not kept: this run uses a temporary folder."));
    rememberView();
    QTimer::singleShot(600, this, [this] { renderBuiltInThumbnails(); });
}

MainWindow::~MainWindow()
{
    for (auto it = m_effectsDefault.cbegin(); it != m_effectsDefault.cend(); ++it)
        QApplication::setEffectEnabled(Qt::UIEffect(it.key()), it.value());
}

// ------------------------------------------------------------------------------------------------ menus

void MainWindow::buildMenus()
{
    QMenuBar* bar = menuBar();
    QMenu* file = bar->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("Open Location…"), QKeySequence::Open, this, &MainWindow::openLocation);
    file->addAction(QStringLiteral("Save Location…"), QKeySequence::Save, this, &MainWindow::saveLocation);
    file->addAction(QStringLiteral("Save Picture…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), this, &MainWindow::savePicture);
    file->addSeparator();
    file->addAction(QStringLiteral("Copy Location"), QKeySequence(QStringLiteral("Ctrl+Shift+C")), this, &MainWindow::copyLocation);
    file->addAction(QStringLiteral("Paste Location"), QKeySequence(QStringLiteral("Ctrl+Shift+V")), this, &MainWindow::pasteLocation);
    file->addSeparator();
    QMenu* settings = file->addMenu(QStringLiteral("Settings"));
    settings->addAction(QStringLiteral("Change App Data Folder…"), this, [this] {
        QString msg;
        if (appdata::change(this, &msg)) {
            log(msg);
            m_notKept->setVisible(appdata::isTemporary());
            saveSettingsNow();
            savePlaces();
        } else if (!msg.isEmpty()) {
            log(msg);
        }
    });
    settings->addAction(QStringLiteral("Reset Settings to Defaults…"), this, [this] {
        if (m_interactive && QMessageBox::question(this, QStringLiteral("Reset Settings"),
                                                   QStringLiteral("Put every setting back to its default? Saved places are kept.")) != QMessageBox::Yes)
            return;
        m_s = Settings();
        m_fractal = core::FractalParams();
        m_colors = core::ColorParams();
        syncControls();
        m_view->setFractal(m_fractal);
        m_view->setColors(m_colors);
        m_view->setAntiAliasing(m_s.antiAliasing);
        m_view->setJuliaPreview(m_s.juliaPreview);
        m_view->setZoomStep(1.5);
        m_view->setView(core::View::home(false));
        for (const QString& c : m_cards.keys()) setCardOpen(c, !m_s.foldedCards.contains(c), false);
        applyTheme();
        applyOpacity();
        log(QStringLiteral("Settings are back to their defaults."));
        saveSettingsSoon();
    });
    file->addSeparator();
    file->addAction(QStringLiteral("Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* explore = bar->addMenu(QStringLiteral("&Explore"));
    m_backAct = explore->addAction(QStringLiteral("Back"), QKeySequence(QStringLiteral("Alt+Left")), this, &MainWindow::back);
    m_forwardAct = explore->addAction(QStringLiteral("Forward"), QKeySequence(QStringLiteral("Alt+Right")), this, &MainWindow::forward);
    explore->addAction(QStringLiteral("Reset View"), QKeySequence(Qt::Key_Home), this, &MainWindow::resetView);
    explore->addSeparator();
    explore->addAction(QStringLiteral("Zoom In"), QKeySequence(QStringLiteral("Ctrl+=")), this, [this] { m_view->zoomAnimated(2.0); });
    explore->addAction(QStringLiteral("Zoom Out"), QKeySequence(QStringLiteral("Ctrl+-")), this, [this] { m_view->zoomAnimated(0.5); });
    explore->addAction(QStringLiteral("Turn Left"), QKeySequence(QStringLiteral("Ctrl+[")), this, [this] { m_view->rotateBy(-15); });
    explore->addAction(QStringLiteral("Turn Right"), QKeySequence(QStringLiteral("Ctrl+]")), this, [this] { m_view->rotateBy(15); });
    explore->addAction(QStringLiteral("Straighten"), this, [this] {
        core::View v = m_view->view();
        m_view->rotateBy(-v.angle * 180.0 / M_PI);
    });
    explore->addSeparator();
    explore->addAction(QStringLiteral("Add to Places…"), QKeySequence(QStringLiteral("Ctrl+D")), this, &MainWindow::addPlace);
    explore->addAction(QStringLiteral("Randomize Everything"), QKeySequence(QStringLiteral("Ctrl+R")), this, &MainWindow::randomizeEverything);
    explore->addAction(QStringLiteral("Randomize Colors"), QKeySequence(QStringLiteral("Ctrl+Shift+R")), this, &MainWindow::randomizeColors);
    explore->addSeparator();
    m_autoAct = explore->addAction(QStringLiteral("Autopilot"));
    m_autoAct->setShortcut(QKeySequence(Qt::Key_P));
    m_autoAct->setCheckable(true);
    m_autoAct->setToolTip(QStringLiteral("Zooms in on its own, forever, steering toward the richest detail."));
    connect(m_autoAct, &QAction::triggered, this, [this](bool on) { setAutopilot(on); });
    m_pauseAct = explore->addAction(QStringLiteral("Pause Animation"));
    m_pauseAct->setCheckable(true);
    m_pauseAct->setToolTip(QStringLiteral("Stops the autopilot and color cycling where they are (Space on the picture)."));
    connect(m_pauseAct, &QAction::triggered, this, [this](bool on) { setAnimationPaused(on); });
    QMenu* apSpeed = explore->addMenu(QStringLiteral("Autopilot Speed"));
    auto* apGroup = new QActionGroup(this);
    const QList<QPair<QString, double>> apSpeeds = {{QStringLiteral("Slow"), 1.25}, {QStringLiteral("Normal"), 2.0},
                                                    {QStringLiteral("Fast"), 4.0}, {QStringLiteral("Very Fast"), 8.0}};
    for (const auto& [name, f] : apSpeeds) {
        QAction* a = apSpeed->addAction(name);
        a->setCheckable(true);
        a->setChecked(f == 2.0);
        a->setToolTip(QStringLiteral("Zooms %1 times in every second.").arg(f));
        apGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, f = f] { m_view->setAutopilotSpeed(f); });
    }
    explore->addAction(QStringLiteral("Record Zoom…"), QKeySequence(QStringLiteral("Ctrl+M")), this, &MainWindow::recordZoom);
    connect(explore, &QMenu::aboutToShow, this, [this] {
        m_backAct->setEnabled(m_historyPos > 0);
        m_forwardAct->setEnabled(m_historyPos >= 0 && m_historyPos < int(m_history.size()) - 1);
    });

    QMenu* edit = bar->addMenu(QStringLiteral("&Edit"));
    m_undoAct = edit->addAction(QStringLiteral("Undo"), QKeySequence::Undo, this, &MainWindow::undo);
    m_redoAct = edit->addAction(QStringLiteral("Redo"), QKeySequence(QStringLiteral("Ctrl+Y")), this, &MainWindow::redo);
    m_undoAct->setToolTip(QStringLiteral("Puts back the colors and formula as they were before the last change."));
    edit->addSeparator();
    edit->addAction(QStringLiteral("Reset Colors to Defaults"), this, [this] {
        finishEdit();
        setColors(core::ColorParams());
        finishEdit();
        log(QStringLiteral("Colors are back to their defaults (the Seahorse Classic look). Undo brings yours back."));
    });
    edit->addAction(QStringLiteral("Reset Everything to Defaults"), this, [this] {
        finishEdit();
        noteEdit();
        m_restoring = true;
        m_fractal = core::FractalParams();
        m_colors = core::ColorParams();
        syncControls();
        m_view->setFractal(m_fractal);
        m_view->setColors(m_colors);
        m_restoring = false;
        m_view->setView(core::View::home(false));
        rememberView();
        m_lastState = currentSettings();
        log(QStringLiteral("Formula, colors and view are back to their defaults. Undo and Back bring yours back."));
        saveSettingsSoon();
    });
    connect(edit, &QMenu::aboutToShow, this, [this] {
        m_undoAct->setEnabled(!m_undo.isEmpty());
        m_redoAct->setEnabled(!m_redo.isEmpty());
    });

    m_viewMenu = bar->addMenu(QStringLiteral("&View"));
    QMenu* theme = m_viewMenu->addMenu(QStringLiteral("&Theme"));
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);
    for (const QString& g : themes::groups()) {
        QMenu* sub = theme->addMenu(g);
        QList<const themes::Theme*> list;
        for (const themes::Theme& t : themes::all())
            if (t.group == g) list << &t;
        std::sort(list.begin(), list.end(), [](auto* a, auto* b) { return a->name.compare(b->name, Qt::CaseInsensitive) < 0; });
        for (auto* t : list) addThemeAction(sub, t->name, t->key);
    }
    addThemeAction(theme, QStringLiteral("Match Operating System"), QStringLiteral("system"));
    connect(m_viewMenu, &QMenu::aboutToHide, this, [this] { endThemePreview(); });
    connect(theme, &QMenu::aboutToHide, this, [this] { endThemePreview(); });
    connect(theme, &QMenu::hovered, this, [this](QAction* a) {
        const bool isTheme = !a->menu() && m_themeGroup->actions().contains(a);
        if (!isTheme && !m_themePreviewing.isEmpty() && m_themePreviewing != m_s.theme) previewTheme(m_s.theme);
    });

    // Window Opacity: one slider, applied to every part of the app.
    QMenu* opacity = m_viewMenu->addMenu(QStringLiteral("Window Opacity"));
    auto* row = new QWidget;
    row->setObjectName(QStringLiteral("menuRow"));
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(12, 6, 12, 6);
    auto* ol = new QLabel(QStringLiteral("Opacity"));
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(30, 100);
    slider->setValue(m_s.windowOpacity);
    slider->setMinimumWidth(160);
    slider->setProperty("testId", QStringLiteral("opacitySlider"));
    auto* pct = new QLabel(QStringLiteral("%1 %").arg(m_s.windowOpacity));
    pct->setMinimumWidth(pct->fontMetrics().horizontalAdvance(QStringLiteral("100 %")));
    rl->addWidget(ol);
    rl->addWidget(slider, 1);
    rl->addWidget(pct);
    auto* wa = new QWidgetAction(opacity);
    wa->setDefaultWidget(row);
    opacity->addAction(wa);
    connect(slider, &QSlider::valueChanged, this, [this, pct](int v) {
        m_s.windowOpacity = v;
        pct->setText(QStringLiteral("%1 %").arg(v));
        applyOpacity();
        saveSettingsSoon();
    });
    m_viewMenu->addSeparator();

    m_juliaPreviewAct = m_viewMenu->addAction(QStringLiteral("Julia Preview"));
    m_juliaPreviewAct->setShortcut(QKeySequence(Qt::Key_J));
    m_juliaPreviewAct->setCheckable(true);
    m_juliaPreviewAct->setChecked(m_s.juliaPreview);
    m_juliaPreviewAct->setToolTip(QStringLiteral("Shows the Julia set of the point under the pointer in a corner of the picture."));
    connect(m_juliaPreviewAct, &QAction::toggled, this, [this](bool on) {
        m_s.juliaPreview = on;
        m_view->setJuliaPreview(on);
        saveSettingsSoon();
    });
    QMenu* aa = m_viewMenu->addMenu(QStringLiteral("Anti-Aliasing"));
    m_aaGroup = new QActionGroup(this);
    const QStringList aaNames = {QStringLiteral("Off"), QStringLiteral("2 × 2"), QStringLiteral("3 × 3"), QStringLiteral("4 × 4")};
    for (int i = 1; i <= 4; ++i) {
        QAction* a = aa->addAction(aaNames[i - 1]);
        a->setCheckable(true);
        a->setChecked(m_s.antiAliasing == i);
        a->setData(i);
        m_aaGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] {
            m_s.antiAliasing = i;
            m_view->setAntiAliasing(i);
            saveSettingsSoon();
        });
    }
    QMenu* speed = m_viewMenu->addMenu(QStringLiteral("Zoom Speed"));
    auto* speedGroup = new QActionGroup(this);
    const QList<QPair<QString, int>> speeds = {{QStringLiteral("Gentle"), 50}, {QStringLiteral("Normal"), 100},
                                               {QStringLiteral("Fast"), 200}, {QStringLiteral("Very Fast"), 350}};
    for (const auto& [name, pctv] : speeds) {
        QAction* a = speed->addAction(name);
        a->setCheckable(true);
        a->setChecked(m_s.zoomSpeedPct == pctv);
        speedGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, v = pctv] {
            m_s.zoomSpeedPct = v;
            m_view->setZoomStep(std::pow(1.5, v / 100.0));
            saveSettingsSoon();
        });
    }
    m_viewMenu->addSeparator();
    m_onTopAct = m_viewMenu->addAction(QStringLiteral("Always on Top"));
    m_onTopAct->setCheckable(true);
    m_onTopAct->setChecked(m_s.alwaysOnTop);
    connect(m_onTopAct, &QAction::toggled, this, [this](bool on) {
        m_s.alwaysOnTop = on;
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        show();
        applyOpacity();
        saveSettingsSoon();
    });
    QAction* full = m_viewMenu->addAction(QStringLiteral("Full Screen"));
    full->setShortcut(QKeySequence(Qt::Key_F11));
    full->setCheckable(true);
    connect(full, &QAction::toggled, this, [this](bool on) { on ? showFullScreen() : showNormal(); });
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(QStringLiteral("Reset View Options"), this, [this] {
        m_s.resetView();
        applyTheme();
        for (QAction* a : m_themeGroup->actions()) a->setChecked(a->data().toString() == m_s.theme);
        for (QAction* a : m_aaGroup->actions()) a->setChecked(a->data().toInt() == m_s.antiAliasing);
        m_juliaPreviewAct->setChecked(m_s.juliaPreview);
        m_onTopAct->setChecked(m_s.alwaysOnTop);
        m_view->setAntiAliasing(m_s.antiAliasing);
        m_view->setZoomStep(1.5);
        for (const QString& c : m_cards.keys()) setCardOpen(c, !m_s.foldedCards.contains(c), false);
        applyOpacity();
        saveSettingsSoon();
    });

    QMenu* help = bar->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("Mouse and Keys"), QKeySequence(Qt::Key_F1), this, [this] {
        QMessageBox::information(this, QStringLiteral("Mouse and Keys"),
            QStringLiteral("Scroll: zoom in or out at the pointer\nDrag: move\nRight-drag: turn\nDouble-click: zoom in 4×\n"
                           "Right-click: zoom here, center here, the Julia set here, copy the point\n\n"
                           "Arrow keys: move\n+ and -: zoom\nPage Up and Page Down: turn\nHome: reset the view\n"
                           "Alt+Left and Alt+Right: back and forward\nJ: Julia preview\nP: autopilot\n"
                           "Space: pause or resume the autopilot and color cycling\nEscape: stop the autopilot\n"
                           "Ctrl+Z and Ctrl+Y: undo and redo a change of colors or formula\n"
                           "Ctrl+R: randomize everything\nF11: full screen"));
    });
    help->addAction(QStringLiteral("Project Website"), this, [] { QDesktopServices::openUrl(QUrl(kWebsite)); });
    help->addAction(QStringLiteral("Open App Data Folder"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(appdata::folder()));
    });
    help->addAction(QStringLiteral("Report a Problem"), this, [] { QDesktopServices::openUrl(QUrl(kWebsite + QStringLiteral("/issues"))); });
    help->addAction(QStringLiteral("About"), this, [this] {
        QMessageBox::about(this, QStringLiteral("About " APP_TITLE),
            QStringLiteral("<b>" APP_TITLE "</b> %1<br><br>Explore the Mandelbrot and Julia sets, zooming as deep as you like."
                           "<br><br>Copyright (C) 2026 cincottash. Licensed under the GNU GPL, version 3."
                           "<br><a href=\"%2\">%2</a>").arg(QStringLiteral(APP_VERSION), kWebsite));
    });
}

QAction* MainWindow::addThemeAction(QMenu* menu, const QString& text, const QString& key)
{
    QAction* a = menu->addAction(text);
    a->setCheckable(true);
    a->setChecked(m_s.theme == key);
    a->setData(key);
    m_themeGroup->addAction(a);
    connect(a, &QAction::hovered, this, [this, key] { previewTheme(key); });
    connect(a, &QAction::triggered, this, [this, key] {
        m_s.theme = key;
        saveSettingsSoon();
        applyTheme();
    });
    return a;
}

void MainWindow::applyTheme()
{
    QWidget* c = centralWidget();
    if (c && !c->styleSheet().isEmpty()) {
        c->setStyleSheet(QString());
        c->setPalette(QPalette());
    }
    themes::apply(themes::forChoice(m_s.theme));
}

void MainWindow::previewTheme(const QString& key)
{
    m_themePreviewing = key;
    m_themePreviewTimer.start();
}

void MainWindow::endThemePreview()
{
    m_themePreviewTimer.stop();
    if (m_themePreviewing.isEmpty()) return;
    m_themePreviewing.clear();
    QTimer::singleShot(0, this, [this] { applyTheme(); });
}

bool MainWindow::followsOpacity(const QWidget* w) const { return w && w != this && w->isWindow(); }

void MainWindow::applyOpacity()
{
    const double level = m_s.windowOpacity / 100.0;
    filedialogs::opacityPercent() = m_s.windowOpacity;
    if (std::fabs(windowOpacity() - level) > 0.01) setWindowOpacity(level);
    const bool solid = level >= 0.995;
    for (auto it = m_effectsDefault.cbegin(); it != m_effectsDefault.cend(); ++it) {
        const bool want = solid && it.value();
        if (QApplication::isEffectEnabled(Qt::UIEffect(it.key())) != want) QApplication::setEffectEnabled(Qt::UIEffect(it.key()), want);
    }
    QList<QWidget*> targets = QApplication::topLevelWidgets();
    for (QMenu* m : findChildren<QMenu*>()) targets << m;
    for (QWidget* w : targets)
        if (followsOpacity(w) && std::fabs(w->windowOpacity() - level) > 0.01) w->setWindowOpacity(level);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e)
{
    if ((e->type() == QEvent::Show || e->type() == QEvent::Polish) && watched->isWidgetType()) {
        auto* w = static_cast<QWidget*>(watched);
        if (followsOpacity(w)) w->setWindowOpacity(m_s.windowOpacity / 100.0);
    }
    return QMainWindow::eventFilter(watched, e);
}

void MainWindow::trigger(const QString& menuText)
{
    for (QAction* a : findChildren<QAction*>())
        if (!a->menu() && a->text() == menuText) {
            a->trigger();
            return;
        }
}

// ------------------------------------------------------------------------------------------------ layout

QToolButton* MainWindow::linkButton(const QString& text)
{
    auto* b = new QToolButton;
    b->setObjectName(QStringLiteral("link"));
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QFrame* MainWindow::makeCard(const QString& title, QVBoxLayout** body)
{
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    auto* lay = new QVBoxLayout(frame);
    lay->setContentsMargins(14, 10, 14, 12);
    lay->setSpacing(8);
    auto* head = new QHBoxLayout;
    auto* t = new QLabel(title);
    t->setObjectName(QStringLiteral("cardTitle"));
    head->addWidget(t);
    head->addStretch();
    static const QStringList resettable = {QStringLiteral("FRACTAL"), QStringLiteral("COLOR"), QStringLiteral("SINE WAVES"),
                                           QStringLiteral("LIGHTING"), QStringLiteral("EDGES"), QStringLiteral("INTERIOR"),
                                           QStringLiteral("EFFECTS"), QStringLiteral("SPHERE")};
    if (resettable.contains(title)) {
        auto* reset = linkButton(QStringLiteral("Reset"));
        reset->setProperty("testId", QStringLiteral("reset ") + title);
        reset->setToolTip(QStringLiteral("Puts this card's settings back to their defaults. Undo (Ctrl+Z) brings yours back."));
        connect(reset, &QToolButton::clicked, this, [this, title] { resetCard(title); });
        head->addWidget(reset);
        head->addSpacing(10);
    }
    auto* toggle = linkButton(QStringLiteral("Hide ▾"));
    toggle->setProperty("testId", QStringLiteral("fold ") + title);
    head->addWidget(toggle);
    lay->addLayout(head);
    auto* b = new QWidget;
    auto* bl = new QVBoxLayout(b);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    lay->addWidget(b, 1);
    *body = bl;
    m_cards.insert(title, {frame, b, toggle});
    connect(toggle, &QToolButton::clicked, this, [this, title] {
        const Card& c = m_cards[title];
        setCardOpen(title, !c.body->isVisibleTo(c.frame));
    });
    return frame;
}

void MainWindow::setCardOpen(const QString& name, bool open, bool save)
{
    if (!m_cards.contains(name)) return;
    const Card& c = m_cards[name];
    c.body->setVisible(open);
    c.toggle->setText(open ? QStringLiteral("Hide ▾") : QStringLiteral("Show ▴"));
    c.frame->setSizePolicy(QSizePolicy::Preferred, open ? QSizePolicy::Preferred : QSizePolicy::Maximum);
    m_s.foldedCards.removeAll(name);
    if (!open) m_s.foldedCards << name;
    updateStretch();
    if (save) saveSettingsSoon();
}

bool MainWindow::cardOpen(const QString& name) const
{
    if (!m_cards.contains(name)) return false;
    const Card& c = m_cards[name];
    return c.body->isVisibleTo(c.frame);
}

void MainWindow::updateStretch()
{
    // Folded cards stack at the top; the spare height goes below the last card.
    if (m_leftCol) m_leftCol->setStretch(m_leftCol->count() - 1, 1);
}

QGridLayout* MainWindow::sliderGrid(const char* card, QVBoxLayout* into)
{
    auto* grid = cardGrid();
    int r = 0;
    for (const core::SliderSpec& sp : core::sliderSpecs()) {
        if (QLatin1String(sp.card) != QLatin1String(card)) continue;
        auto* row = new SliderRow(QString::fromUtf8(sp.label), sp.min, sp.max, sp.def, sp.decimals, QString::fromUtf8(sp.unit),
                                  sp.logScale, QString::fromUtf8(sp.tip), this);
        row->setTestId(QString::fromLatin1(sp.key));
        row->addTo(grid, r++);
        m_rows.insert(QString::fromLatin1(sp.key), row);
        const auto member = sp.member;
        connect(row, &SliderRow::valueChanged, this, [this, member](double v) {
            m_colors.*member = v;
            colorsEdited();
        });
    }
    into->addLayout(grid);
    return grid;
}

QWidget* MainWindow::buildFractalCard()
{
    QVBoxLayout* body = nullptr;
    QFrame* card = makeCard(QStringLiteral("FRACTAL"), &body);
    auto* grid = cardGrid();
    int r = 0;
    grid->addWidget(new QLabel(QStringLiteral("Type")), r, 0);
    m_type = new QComboBox;
    m_type->addItems({QStringLiteral("Mandelbrot Set"), QStringLiteral("Julia Set")});
    m_type->setProperty("testId", QStringLiteral("type"));
    grid->addWidget(m_type, r++, 1, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("Formula")), r, 0);
    m_formula = new QComboBox;
    m_formula->addItems(core::formulaNames());
    m_formula->setProperty("testId", QStringLiteral("formula"));
    m_formula->setItemData(0, QStringLiteral("z^power + c: the Mandelbrot set itself."), Qt::ToolTipRole);
    m_formula->setItemData(1, QStringLiteral("(|x| + i|y|)^2 + c: folds each step into one quarter of the plane. "
                                             "Shows a burning ship, with fleets of smaller ships along its mast."), Qt::ToolTipRole);
    m_formula->setItemData(2, QStringLiteral("conj(z)^power + c, the Mandelbar: three-cornered at power 2, more corners "
                                             "above."), Qt::ToolTipRole);
    m_formula->setToolTip(QStringLiteral("The rule that takes each point one step further. The Burning Ship and the Tricorn "
                                         "zoom to about 1e-24."));
    grid->addWidget(m_formula, r++, 1, 1, 2);
    m_power = new SliderRow(QStringLiteral("Power"), 2, 8, 2, 0, QString(), false,
                            QStringLiteral("The exponent in z^power + c. Power 2 is the classic set and zooms without limit; "
                                           "higher powers zoom to about 1e-24. The Burning Ship is always power 2."), this);
    m_power->setTestId(QStringLiteral("power"));
    m_power->addTo(grid, r++);
    m_escape = new SliderRow(QStringLiteral("Escape Radius"), 2, 100000, 256, 0, QString(), true,
                             QStringLiteral("How far a point must travel before it counts as escaped. Small values show "
                                            "the bands' edges; large values give the smoothest colors."), this);
    m_escape->setTestId(QStringLiteral("escapeRadius"));
    m_escape->addTo(grid, r++);
    grid->addWidget(new QLabel(QStringLiteral("Iterations")), r, 0);
    auto* iterRow = new QHBoxLayout;
    m_autoIter = new QCheckBox(QStringLiteral("Auto"));
    m_autoIter->setToolTip(QStringLiteral("Picks the limit from the zoom depth, and raises it when many pixels need more."));
    m_autoIter->setProperty("testId", QStringLiteral("autoIterations"));
    m_iter = new QSpinBox;
    m_iter->setRange(core::FractalParams::kMinIterations, core::FractalParams::kMaxIterations);
    m_iter->setGroupSeparatorShown(true);
    m_iter->setKeyboardTracking(false);
    m_iter->setProperty("testId", QStringLiteral("iterations"));
    m_iter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    iterRow->addWidget(m_autoIter);
    iterRow->addWidget(m_iter, 1);
    grid->addLayout(iterRow, r++, 1, 1, 2);
    body->addLayout(grid);

    m_seedBox = new QWidget;
    auto* sl = new QHBoxLayout(m_seedBox);
    sl->setContentsMargins(0, 2, 0, 0);
    m_seedPad = new SeedPad;
    sl->addWidget(m_seedPad);
    auto* fields = new QGridLayout;
    fields->setVerticalSpacing(6);
    auto* seedTitle = new QLabel(QStringLiteral("Julia Seed"));
    fields->addWidget(seedTitle, 0, 0, 1, 2);
    fields->addWidget(new QLabel(QStringLiteral("Re")), 1, 0);
    fields->addWidget(new QLabel(QStringLiteral("Im")), 2, 0);
    m_seedRe = new QLineEdit;
    m_seedIm = new QLineEdit;
    m_seedRe->setProperty("testId", QStringLiteral("seedRe"));
    m_seedIm->setProperty("testId", QStringLiteral("seedIm"));
    for (QLineEdit* e : {m_seedRe, m_seedIm}) e->setToolTip(QStringLiteral("Any number of digits; press Enter to use it."));
    fields->addWidget(m_seedRe, 1, 1);
    fields->addWidget(m_seedIm, 2, 1);
    fields->setRowStretch(3, 1);
    sl->addLayout(fields, 1);
    body->addWidget(m_seedBox);
    m_juliaHint = new QLabel(QStringLiteral("Right-click the picture for the Julia set of any point, or press J for a "
                                            "live Julia preview."));
    m_juliaHint->setObjectName(QStringLiteral("muted"));
    m_juliaHint->setWordWrap(true);
    body->addWidget(m_juliaHint);

    connect(m_type, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing) return;
        if (i == 1) switchToJulia(m_fractal.juliaRe, m_fractal.juliaIm);
        else switchToMandelbrot();
    });
    connect(m_formula, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing) return;
        switchFormula(core::Formula(i));
    });
    connect(m_power, &SliderRow::valueChanged, this, [this](double v) {
        m_fractal.power = int(std::lround(v));
        fractalEdited();
    });
    connect(m_escape, &SliderRow::valueChanged, this, [this](double v) {
        m_fractal.escapeRadius = v;
        fractalEdited();
    });
    connect(m_autoIter, &QCheckBox::toggled, this, [this](bool on) {
        if (m_syncing) return;
        m_fractal.autoIterations = on;
        if (!on) m_fractal.maxIterations = m_iter->value();
        m_iter->setEnabled(!on);
        fractalEdited();
    });
    connect(m_iter, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_syncing || m_fractal.autoIterations) return;
        m_fractal.maxIterations = v;
        fractalEdited();
    });
    connect(m_seedPad, &SeedPad::seedChanged, this, [this](double re, double im) {
        m_fractal.juliaRe = QString::number(re, 'f', 4);
        m_fractal.juliaIm = QString::number(im, 'f', 4);
        m_syncing = true;
        m_seedRe->setText(m_fractal.juliaRe);
        m_seedIm->setText(m_fractal.juliaIm);
        m_syncing = false;
        fractalEdited();
    });
    auto seedTyped = [this](QLineEdit* e, QString* target) {
        connect(e, &QLineEdit::editingFinished, this, [this, e, target] {
            core::BigFloat probe(64);
            const QString t = e->text().trimmed();
            if (!probe.parse(t.toStdString()) || std::fabs(probe.toDouble()) > 4) {
                log(QStringLiteral("\"%1\" isn't a usable seed number (it must be a plain number between -4 and 4).").arg(t));
                e->setText(*target);
                return;
            }
            if (t == *target) return;
            *target = t;
            m_seedPad->setSeed(m_fractal.seedX(64).toDouble(), m_fractal.seedY(64).toDouble());
            fractalEdited();
        });
    };
    seedTyped(m_seedRe, &m_fractal.juliaRe);
    seedTyped(m_seedIm, &m_fractal.juliaIm);
    return card;
}

QWidget* MainWindow::buildColorCard()
{
    QVBoxLayout* body = nullptr;
    QFrame* card = makeCard(QStringLiteral("COLOR"), &body);
    auto* grid = cardGrid();
    int r = 0;
    grid->addWidget(new QLabel(QStringLiteral("Look")), r, 0);
    m_look = new QComboBox;
    m_look->setProperty("testId", QStringLiteral("look"));
    m_look->setToolTip(QStringLiteral("Ready-made combinations of every color setting. Change anything to make it your own."));
    for (const core::Look& l : core::looks()) m_look->addItem(l.name);
    grid->addWidget(m_look, r++, 1);
    grid->addWidget(new QLabel(QStringLiteral("Style")), r, 0);
    m_style = new QComboBox;
    m_style->addItems(core::styleNames());
    m_style->setProperty("testId", QStringLiteral("style"));
    m_style->setToolTip(QStringLiteral("What the colors follow: the escape time (gradient, sine waves), the distance to the "
                                       "set, stripes, the angle of escape, how close the orbit comes to a shape (orbit "
                                       "trap), or the original Python renders' moiré fabric."));
    grid->addWidget(m_style, r++, 1);
    m_paletteLabel = new QLabel(QStringLiteral("Palette"));
    grid->addWidget(m_paletteLabel, r, 0);
    m_palette = new QComboBox;
    m_palette->setProperty("testId", QStringLiteral("palette"));
    m_palette->setIconSize(QSize(64, 14));
    for (const QString& n : core::paletteNames()) m_palette->addItem(paletteIcon(n), n);
    m_palette->setMaxVisibleItems(20);
    grid->addWidget(m_palette, r++, 1);
    m_scaleLabel = new QLabel(QStringLiteral("Color Scale"));
    grid->addWidget(m_scaleLabel, r, 0);
    m_scale = new QComboBox;
    m_scale->addItems(core::colorScaleNames());
    m_scale->setProperty("testId", QStringLiteral("colorScale"));
    m_scale->setToolTip(QStringLiteral("How the escape time maps to colors. Linear keeps bands even at any depth; "
                                       "Logarithmic spreads the first bands out when zoomed far out."));
    grid->addWidget(m_scale, r++, 1);
    m_moireTintLabel = new QLabel(QStringLiteral("Tint"));
    grid->addWidget(m_moireTintLabel, r, 0);
    m_moireTint = new ColorButton(QStringLiteral("Fabric Tint"));
    m_moireTint->setProperty("testId", QStringLiteral("moireTint"));
    grid->addWidget(m_moireTint, r++, 1, Qt::AlignLeft);
    m_trapShapeLabel = new QLabel(QStringLiteral("Trap Shape"));
    grid->addWidget(m_trapShapeLabel, r, 0);
    m_trapShape = new QComboBox;
    m_trapShape->addItems(core::trapShapeNames());
    m_trapShape->setProperty("testId", QStringLiteral("trapShape"));
    m_trapShape->setToolTip(QStringLiteral("Colors each point by how close its orbit comes to this shape: a point gives "
                                           "glowing eyes, a cross gives thin stalks, a circle gives rings. Changing it "
                                           "renders the view again."));
    grid->addWidget(m_trapShape, r++, 1);
    body->addLayout(grid);
    sliderGrid("COLOR", body);
    connect(m_trapShape, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing) return;
        m_colors.trapShape = i;
        colorsEdited();
    });

    connect(m_look, &QComboBox::activated, this, [this](int i) {
        if (i < 0 || i >= core::looks().size()) return;
        m_colors = core::looks()[i].params;
        syncControls();
        colorsEdited();
        log(QStringLiteral("Look: %1.").arg(core::looks()[i].name));
    });
    connect(m_style, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing) return;
        m_colors.style = i;
        updateStyleVisibility();
        colorsEdited();
    });
    connect(m_palette, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing || i < 0) return;
        m_colors.palette = m_palette->itemText(i);
        colorsEdited();
    });
    connect(m_scale, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_syncing) return;
        m_colors.colorScale = i;
        colorsEdited();
    });
    connect(m_moireTint, &ColorButton::colorChanged, this, [this](const QColor& c) {
        m_colors.moireTint = c;
        colorsEdited();
    });
    return card;
}

void MainWindow::buildUi()
{
    auto* root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    auto* outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header
    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("header"));
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(16, 10, 16, 10);
    m_title = new QLabel(QStringLiteral(APP_TITLE));
    m_title->setObjectName(QStringLiteral("appTitle"));
    hl->addWidget(m_title);
    hl->addStretch();
    m_notKept = linkButton(QStringLiteral("Settings Not Kept"));
    m_notKept->setToolTip(QStringLiteral("This run keeps no settings: they are deleted when the app closes. Click to choose a folder."));
    m_notKept->setVisible(appdata::isTemporary());
    connect(m_notKept, &QToolButton::clicked, this, [this] { trigger(QStringLiteral("Change App Data Folder…")); });
    hl->addWidget(m_notKept);
    m_pill = new QLabel(QStringLiteral("● Starting"));
    m_pill->setObjectName(QStringLiteral("pill"));
    m_pill->setProperty("tone", QStringLiteral("idle"));
    m_pill->setProperty("testId", QStringLiteral("pill"));
    hl->addWidget(m_pill);
    outer->addWidget(header);

    // Body: cards on the left, the picture on the right
    auto* bodyW = new QWidget;
    auto* bl = new QHBoxLayout(bodyW);
    bl->setContentsMargins(12, 12, 12, 12);
    bl->setSpacing(12);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* leftW = new QWidget;
    m_leftCol = new QVBoxLayout(leftW);
    m_leftCol->setContentsMargins(0, 0, 4, 0);
    m_leftCol->setSpacing(10);
    m_leftCol->addWidget(buildFractalCard());
    m_leftCol->addWidget(buildColorCard());
    {
        QVBoxLayout* b = nullptr;
        m_leftCol->addWidget(makeCard(QStringLiteral("SINE WAVES"), &b));
        sliderGrid("SINE WAVES", b);
    }
    {
        QVBoxLayout* b = nullptr;
        m_leftCol->addWidget(makeCard(QStringLiteral("LIGHTING"), &b));
        sliderGrid("LIGHTING", b);
    }
    {
        QVBoxLayout* b = nullptr;
        m_leftCol->addWidget(makeCard(QStringLiteral("EDGES"), &b));
        QGridLayout* g = sliderGrid("EDGES", b);
        const int r = g->rowCount();
        g->addWidget(new QLabel(QStringLiteral("Edge Color")), r, 0);
        m_edgeColor = new ColorButton(QStringLiteral("Edge Color"));
        m_edgeColor->setProperty("testId", QStringLiteral("edgeColor"));
        g->addWidget(m_edgeColor, r, 1, 1, 2, Qt::AlignLeft);
        connect(m_edgeColor, &ColorButton::colorChanged, this, [this](const QColor& c) {
            m_colors.edgeColor = c;
            colorsEdited();
        });
    }
    {
        QVBoxLayout* b = nullptr;
        m_leftCol->addWidget(makeCard(QStringLiteral("INTERIOR"), &b));
        auto* g = cardGrid();
        g->addWidget(new QLabel(QStringLiteral("Style")), 0, 0);
        m_interior = new QComboBox;
        m_interior->addItems(core::interiorStyleNames());
        m_interior->setProperty("testId", QStringLiteral("interiorStyle"));
        m_interior->setToolTip(QStringLiteral("How the inside of the set is colored. Diagonal Gradient is the original "
                                              "Python renders' look; Depth shades by how close the orbit came to zero."));
        g->addWidget(m_interior, 0, 1);
        g->addWidget(new QLabel(QStringLiteral("Color")), 1, 0);
        m_in1 = new ColorButton(QStringLiteral("Inside Color"));
        m_in1->setProperty("testId", QStringLiteral("interiorColor"));
        g->addWidget(m_in1, 1, 1, Qt::AlignLeft);
        m_in2Label = new QLabel(QStringLiteral("Second Color"));
        g->addWidget(m_in2Label, 2, 0);
        m_in2 = new ColorButton(QStringLiteral("Second Inside Color"));
        m_in2->setProperty("testId", QStringLiteral("interiorColor2"));
        g->addWidget(m_in2, 2, 1, Qt::AlignLeft);
        b->addLayout(g);
        connect(m_interior, &QComboBox::currentIndexChanged, this, [this](int i) {
            if (m_syncing) return;
            m_colors.interiorStyle = i;
            updateStyleVisibility();
            colorsEdited();
        });
        connect(m_in1, &ColorButton::colorChanged, this, [this](const QColor& c) { m_colors.interiorColor = c; colorsEdited(); });
        connect(m_in2, &ColorButton::colorChanged, this, [this](const QColor& c) { m_colors.interiorColor2 = c; colorsEdited(); });
    }
    {
        QVBoxLayout* b = nullptr;
        m_leftCol->addWidget(makeCard(QStringLiteral("EFFECTS"), &b));
        sliderGrid("EFFECTS", b);
    }
    {
        QVBoxLayout* b = nullptr;
        m_leftCol->addWidget(makeCard(QStringLiteral("SPHERE"), &b));
        QGridLayout* g = sliderGrid("SPHERE", b);
        const int r = g->rowCount();
        g->addWidget(new QLabel(QStringLiteral("Background")), r, 0);
        m_sphereBg = new ColorButton(QStringLiteral("Background Behind the Ball"));
        m_sphereBg->setProperty("testId", QStringLiteral("sphereBackground"));
        g->addWidget(m_sphereBg, r, 1, 1, 2, Qt::AlignLeft);
        connect(m_sphereBg, &ColorButton::colorChanged, this, [this](const QColor& c) {
            m_colors.sphereBackground = c;
            colorsEdited();
        });
    }
    {
        QVBoxLayout* b = nullptr;
        m_leftCol->addWidget(makeCard(QStringLiteral("PLACES"), &b));
        buildPlaces(b);
    }
    {
        QVBoxLayout* b = nullptr;
        m_leftCol->addWidget(makeCard(QStringLiteral("ACTIVITY"), &b));
        m_log = new QPlainTextEdit;
        m_log->setReadOnly(true);
        m_log->setMaximumBlockCount(2000);
        m_log->setMinimumHeight(140);
        m_log->setProperty("testId", QStringLiteral("log"));
        b->addWidget(m_log);
    }
    m_leftCol->addStretch(1);
    scroll->setWidget(leftW);
    // The column is as wide as its widest card, and never squeezes a slider.
    leftW->adjustSize();
    scroll->setFixedWidth(400);
    bl->addWidget(scroll);

    auto* right = new QVBoxLayout;
    right->setSpacing(6);
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("card"));
    auto* fl = new QVBoxLayout(frame);
    fl->setContentsMargins(1, 1, 1, 1);
    m_view = new FractalView;
    fl->addWidget(m_view);
    m_gpuMessage = new QLabel(m_view);
    m_gpuMessage->setWordWrap(true);
    m_gpuMessage->setAlignment(Qt::AlignCenter);
    m_gpuMessage->setStyleSheet(QStringLiteral("color: #E6E8EB; font-size: 11pt; padding: 24px;"));
    m_gpuMessage->hide();
    right->addWidget(frame, 1);
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("muted"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setProperty("testId", QStringLiteral("status"));
    right->addWidget(m_status);
    bl->addLayout(right, 1);
    outer->addWidget(bodyW, 1);

    // Bottom bar
    auto* bottom = new QFrame;
    bottom->setObjectName(QStringLiteral("bottomBar"));
    auto* bb = new QHBoxLayout(bottom);
    bb->setContentsMargins(16, 10, 16, 10);
    auto* save = new QPushButton(QStringLiteral("Save Picture…"));
    save->setProperty("testId", QStringLiteral("savePicture"));
    connect(save, &QPushButton::clicked, this, &MainWindow::savePicture);
    auto* random = new QPushButton(QStringLiteral("Randomize"));
    random->setProperty("testId", QStringLiteral("randomize"));
    random->setToolTip(QStringLiteral("A random fractal, spot, zoom and colors, every time (Ctrl+R). Undo and Back "
                                      "return to the ones before."));
    connect(random, &QPushButton::clicked, this, &MainWindow::randomizeEverything);
    bb->addWidget(save);
    bb->addWidget(random);
    bb->addStretch();
    m_backBtn = new QPushButton(QStringLiteral("◀ Back"));
    m_backBtn->setProperty("testId", QStringLiteral("back"));
    m_backBtn->setToolTip(QStringLiteral("The previous view (Alt+Left)."));
    m_forwardBtn = new QPushButton(QStringLiteral("Forward ▶"));
    m_forwardBtn->setProperty("testId", QStringLiteral("forward"));
    m_forwardBtn->setToolTip(QStringLiteral("The next view again, after going back (Alt+Right)."));
    auto* reset = new QPushButton(QStringLiteral("Reset View"));
    reset->setProperty("testId", QStringLiteral("resetView"));
    reset->setToolTip(QStringLiteral("Zoom all the way out (Home)."));
    auto* record = new QPushButton(QStringLiteral("Record Zoom…"));
    record->setProperty("testId", QStringLiteral("recordZoom"));
    record->setToolTip(QStringLiteral("Makes a movie of a zoom into this view, or a seamless loop (Ctrl+M)."));
    m_autoBtn = new QPushButton(QStringLiteral("Autopilot ▶"));
    m_autoBtn->setObjectName(QStringLiteral("primary"));
    m_autoBtn->setProperty("testId", QStringLiteral("autopilot"));
    m_autoBtn->setToolTip(QStringLiteral("Zooms in on its own, forever, steering toward the richest detail (P). "
                                         "Drag to steer it; Escape stops it."));
    connect(m_backBtn, &QPushButton::clicked, this, &MainWindow::back);
    connect(m_forwardBtn, &QPushButton::clicked, this, &MainWindow::forward);
    connect(reset, &QPushButton::clicked, this, &MainWindow::resetView);
    connect(record, &QPushButton::clicked, this, &MainWindow::recordZoom);
    // One button starts the autopilot, then pauses and resumes it; Stop ends it.
    connect(m_autoBtn, &QPushButton::clicked, this, [this] {
        if (!m_view->autopilot()) setAutopilot(true);
        else setAnimationPaused(!m_view->paused());
    });
    m_stopBtn = new QPushButton(QStringLiteral("■ Stop"));
    m_stopBtn->setProperty("testId", QStringLiteral("stop"));
    m_stopBtn->setToolTip(QStringLiteral("Ends the autopilot here (Escape)."));
    m_stopBtn->hide();
    connect(m_stopBtn, &QPushButton::clicked, this, [this] { setAutopilot(false); });
    bb->addWidget(m_backBtn);
    bb->addWidget(m_forwardBtn);
    bb->addWidget(reset);
    bb->addSpacing(12);
    bb->addWidget(record);
    bb->addWidget(m_stopBtn);
    bb->addWidget(m_autoBtn);
    outer->addWidget(bottom);
    setCentralWidget(root);
}

void MainWindow::buildPlaces(QVBoxLayout* body)
{
    auto* row = new QHBoxLayout;
    m_places = new QListWidget;
    m_places->setIconSize(QSize(80, 50));
    m_places->setMinimumHeight(240);
    m_places->setProperty("testId", QStringLiteral("places"));
    m_places->setContextMenuPolicy(Qt::CustomContextMenu);
    row->addWidget(m_places, 1);
    auto* buttons = new QVBoxLayout;
    m_goPlace = new QPushButton(QStringLiteral("Go To"));
    auto* add = new QPushButton(QStringLiteral("Add…"));
    m_renamePlace = new QPushButton(QStringLiteral("Rename…"));
    m_removePlace = new QPushButton(QStringLiteral("Remove"));
    m_goPlace->setProperty("testId", QStringLiteral("goPlace"));
    add->setProperty("testId", QStringLiteral("addPlace"));
    m_renamePlace->setProperty("testId", QStringLiteral("renamePlace"));
    m_removePlace->setProperty("testId", QStringLiteral("removePlace"));
    add->setToolTip(QStringLiteral("Saves the current view, formula and colors as a place (Ctrl+D)."));
    for (QPushButton* b : {m_goPlace, add, m_renamePlace, m_removePlace}) buttons->addWidget(b);
    buttons->addStretch();
    row->addLayout(buttons);
    body->addLayout(row);
    connect(m_places, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { goToPlace(m_places->row(it)); });
    connect(m_places, &QListWidget::currentRowChanged, this, [this] { updatePlaceButtons(); });
    connect(m_goPlace, &QPushButton::clicked, this, [this] { goToPlace(m_places->currentRow()); });
    connect(add, &QPushButton::clicked, this, &MainWindow::addPlace);
    connect(m_renamePlace, &QPushButton::clicked, this, &MainWindow::renamePlace);
    connect(m_removePlace, &QPushButton::clicked, this, &MainWindow::removePlace);
    connect(m_places, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        QListWidgetItem* it = m_places->itemAt(p);
        if (!it) return;
        m_places->setCurrentItem(it);
        QMenu menu;
        menu.addAction(QStringLiteral("Go To"), this, [this] { goToPlace(m_places->currentRow()); });
        QAction* rn = menu.addAction(QStringLiteral("Rename…"), this, &MainWindow::renamePlace);
        QAction* rm = menu.addAction(QStringLiteral("Remove"), this, &MainWindow::removePlace);
        rn->setEnabled(m_renamePlace->isEnabled());
        rm->setEnabled(m_removePlace->isEnabled());
        menu.exec(m_places->viewport()->mapToGlobal(p));
    });
    updatePlaceButtons();
}

// ------------------------------------------------------------------------------------------------ state

void MainWindow::syncControls()
{
    m_syncing = true;
    m_type->setCurrentIndex(m_fractal.julia() ? 1 : 0);
    m_power->setValue(m_fractal.power);
    m_formula->setCurrentIndex(int(m_fractal.formula));
    m_power->setEnabled(m_fractal.formula != core::Formula::BurningShip);
    m_escape->setValue(m_fractal.escapeRadius);
    m_autoIter->setChecked(m_fractal.autoIterations);
    m_iter->setEnabled(!m_fractal.autoIterations);
    if (!m_fractal.autoIterations) m_iter->setValue(m_fractal.maxIterations);
    m_seedRe->setText(m_fractal.juliaRe);
    m_seedIm->setText(m_fractal.juliaIm);
    m_seedPad->setSeed(m_fractal.seedX(64).toDouble(), m_fractal.seedY(64).toDouble());
    m_style->setCurrentIndex(m_colors.style);
    m_palette->setCurrentIndex(std::max(0, m_palette->findText(m_colors.palette)));
    m_scale->setCurrentIndex(m_colors.colorScale);
    m_moireTint->setColor(m_colors.moireTint);
    m_trapShape->setCurrentIndex(m_colors.trapShape);
    m_edgeColor->setColor(m_colors.edgeColor);
    m_interior->setCurrentIndex(m_colors.interiorStyle);
    m_in1->setColor(m_colors.interiorColor);
    m_in2->setColor(m_colors.interiorColor2);
    m_sphereBg->setColor(m_colors.sphereBackground);
    for (const core::SliderSpec& sp : core::sliderSpecs())
        if (SliderRow* r = m_rows.value(QString::fromLatin1(sp.key))) r->setValue(m_colors.*(sp.member));
    // The Look list names the look the colors match, or "Custom".
    int match = -1;
    for (int i = 0; i < core::looks().size(); ++i)
        if (core::looks()[i].params == m_colors) match = i;
    const int custom = m_look->findText(QStringLiteral("Custom"));
    if (match >= 0) {
        if (custom >= 0) m_look->removeItem(custom);
        m_look->setCurrentIndex(match);
    } else {
        if (custom < 0) m_look->addItem(QStringLiteral("Custom"));
        m_look->setCurrentIndex(m_look->findText(QStringLiteral("Custom")));
    }
    m_syncing = false;
    updateStyleVisibility();
}

void MainWindow::updateStyleVisibility()
{
    const int st = m_colors.style;
    const bool usesPalette = st != int(core::ColorStyle::SineWaves) && st != int(core::ColorStyle::Moire);
    m_palette->setVisible(usesPalette);
    m_paletteLabel->setVisible(usesPalette);
    const bool usesScale = st == int(core::ColorStyle::Gradient) || st == int(core::ColorStyle::SineWaves);
    m_scale->setVisible(usesScale);
    m_scaleLabel->setVisible(usesScale);
    const bool moire = st == int(core::ColorStyle::Moire);
    m_moireTint->setVisible(moire);
    m_moireTintLabel->setVisible(moire);
    const bool trap = st == int(core::ColorStyle::OrbitTrap);
    m_trapShape->setVisible(trap);
    m_trapShapeLabel->setVisible(trap);
    for (const core::SliderSpec& sp : core::sliderSpecs()) {
        SliderRow* r = m_rows.value(QString::fromLatin1(sp.key));
        if (!r || !sp.styles) continue;
        r->setVisible((sp.styles >> st) & 1u);
    }
    // The Sine Waves card only means something for that style.
    if (m_cards.contains(QStringLiteral("SINE WAVES")))
        m_cards[QStringLiteral("SINE WAVES")].frame->setVisible(st == int(core::ColorStyle::SineWaves));
    const bool two = m_colors.interiorStyle != int(core::InteriorStyle::Solid);
    m_in2->setVisible(two);
    m_in2Label->setVisible(two);
    m_seedBox->setVisible(m_fractal.julia());
    m_juliaHint->setVisible(!m_fractal.julia());
    // Settings that only shape an effect wait, grayed, until the effect is on.
    auto enable = [this](const char* key, bool on) {
        if (SliderRow* r = m_rows.value(QString::fromLatin1(key))) r->setEnabled(on);
    };
    enable("edge_width", m_colors.edgeGlow > 0);
    m_edgeColor->setEnabled(m_colors.edgeGlow > 0);
    enable("bloom_size", m_colors.bloom > 0);
    enable("bloom_threshold", m_colors.bloom > 0);
    enable("sphere_shade", m_colors.sphereSize > 0);
    enable("sphere_shine", m_colors.sphereSize > 0);
    enable("trap_size", m_colors.trapShape == int(core::TrapShape::Circle));
    m_sphereBg->setEnabled(m_colors.sphereSize > 0);
}

void MainWindow::noteEdit()
{
    if (m_restoring) return;
    // The first change of a run (moves of a slider within a moment of each other) remembers the state before it.
    if (!m_editBurst.isActive()) {
        m_undo.append(m_lastState);
        if (m_undo.size() > 300) m_undo.removeFirst();
        m_redo.clear();
    }
    m_editBurst.start();
}

void MainWindow::finishEdit()
{
    m_editBurst.stop();
    m_lastState = currentSettings();
}

void MainWindow::restoreSettings(const SettingsState& s)
{
    m_restoring = true;
    m_fractal = s.fractal;
    m_colors = s.colors;
    syncControls();
    m_view->setFractal(m_fractal);
    m_view->setColors(m_colors);
    m_restoring = false;
    m_lastState = s;
    saveSettingsSoon();
}

void MainWindow::undo()
{
    if (m_undo.isEmpty()) return;
    m_editBurst.stop();
    m_redo.append(currentSettings());
    restoreSettings(m_undo.takeLast());
    log(QStringLiteral("Undone."));
}

void MainWindow::redo()
{
    if (m_redo.isEmpty()) return;
    m_editBurst.stop();
    m_undo.append(currentSettings());
    restoreSettings(m_redo.takeLast());
    log(QStringLiteral("Redone."));
}

void MainWindow::resetCard(const QString& title)
{
    finishEdit();   // a run of slider moves before this stays its own Undo step
    const core::ColorParams dc;
    if (title == QLatin1String("FRACTAL")) {
        // The formula's numbers; the type (Mandelbrot or Julia) and the formula stay, so the view still fits.
        core::FractalParams d;
        d.type = m_fractal.type;
        d.formula = m_fractal.formula;
        m_fractal = d;
        syncControls();
        fractalEdited();
    } else {
        for (const core::SliderSpec& sp : core::sliderSpecs())
            if (QLatin1String(sp.card) == title) m_colors.*(sp.member) = dc.*(sp.member);
        if (title == QLatin1String("COLOR")) {
            m_colors.style = dc.style;
            m_colors.trapShape = dc.trapShape;
            m_colors.palette = dc.palette;
            m_colors.colorScale = dc.colorScale;
            m_colors.moireTint = dc.moireTint;
            updateStyleVisibility();
        } else if (title == QLatin1String("EDGES")) {
            m_colors.edgeColor = dc.edgeColor;
        } else if (title == QLatin1String("SPHERE")) {
            m_colors.sphereBackground = dc.sphereBackground;
        } else if (title == QLatin1String("INTERIOR")) {
            m_colors.interiorStyle = dc.interiorStyle;
            m_colors.interiorColor = dc.interiorColor;
            m_colors.interiorColor2 = dc.interiorColor2;
        }
        syncControls();
        colorsEdited();
    }
    finishEdit();
    QString name = title.toLower();
    name[0] = name[0].toUpper();
    log(QStringLiteral("%1 settings are back to their defaults. Undo (Ctrl+Z) brings yours back.").arg(name));
}

void MainWindow::setAnimationPaused(bool on)
{
    m_view->setPaused(on);
    updateAnimationButtons();
}

void MainWindow::updateAnimationButtons()
{
    const bool running = m_view->autopilot();
    const bool paused = m_view->paused();
    m_autoBtn->setText(!running ? QStringLiteral("Autopilot ▶") : paused ? QStringLiteral("▶ Resume") : QStringLiteral("❚❚ Pause"));
    m_autoBtn->setToolTip(!running ? QStringLiteral("Zooms in on its own, forever, steering toward the richest detail (P). "
                                                    "Drag to steer it; Escape stops it.")
                          : paused ? QStringLiteral("Goes on from here (Space on the picture).")
                                   : QStringLiteral("Stops the zoom where it is, without ending the autopilot (Space on the picture)."));
    m_stopBtn->setVisible(running);
    QSignalBlocker block(m_pauseAct);
    m_pauseAct->setChecked(paused);
}

void MainWindow::fractalEdited()
{
    if (m_syncing) return;
    noteEdit();
    m_view->setFractal(m_fractal);
    updateStyleVisibility();
    saveSettingsSoon();
}

void MainWindow::colorsEdited()
{
    if (m_syncing) return;
    noteEdit();
    m_view->setColors(m_colors);
    // Name the look again (or "Custom") without touching the other controls.
    int match = -1;
    for (int i = 0; i < core::looks().size(); ++i)
        if (core::looks()[i].params == m_colors) match = i;
    m_syncing = true;
    const int custom = m_look->findText(QStringLiteral("Custom"));
    if (match >= 0) {
        if (custom >= 0) m_look->removeItem(custom);
        m_look->setCurrentIndex(match);
    } else {
        if (custom < 0) m_look->addItem(QStringLiteral("Custom"));
        m_look->setCurrentIndex(m_look->findText(QStringLiteral("Custom")));
    }
    m_syncing = false;
    updateStyleVisibility();
    saveSettingsSoon();
}

void MainWindow::setColors(const core::ColorParams& c)
{
    m_colors = c;
    syncControls();
    colorsEdited();
}

QString MainWindow::statusText() const { return m_status->text(); }

void MainWindow::updateStatus()
{
    const bool busy = m_view->busy();
    if (!m_view->gpuTried()) {
        m_pill->setText(QStringLiteral("● Starting"));
        setTone(m_pill, "idle");
    } else if (!m_view->gpuReady()) {
        m_pill->setText(QStringLiteral("● Graphics Not Supported"));
        setTone(m_pill, "err");
        m_gpuMessage->setGeometry(m_view->rect());
    } else {
        const render::CoreStatus s = m_view->coreStatus();
        QString gpu = m_view->gpuName();
        if (gpu != m_lastGpuLine) {
            log(QStringLiteral("Drawing with %1.").arg(gpu));
            m_lastGpuLine = gpu;
        }
        if (s.waiting || (s.referenceBusy && s.referenceTarget > 20000)) {
            const int pct = s.referenceTarget > 0 ? int(100.0 * s.referenceProgress / s.referenceTarget) : 0;
            m_pill->setText(QStringLiteral("● Computing Reference %1 %").arg(std::clamp(pct, 0, 100)));
            setTone(m_pill, "warn");
        } else if (m_view->framesPerSecond() > 0.5 && busy) {
            m_pill->setText(QStringLiteral("● %1 · %2 fps").arg(gpu).arg(int(std::lround(m_view->framesPerSecond()))));
            setTone(m_pill, "ok");
        } else {
            m_pill->setText(QStringLiteral("● %1 · Ready").arg(gpu));
            setTone(m_pill, "ok");
        }
        const core::View& v = m_view->view();
        const int digits = std::clamp(int(std::floor(double(v.neededPrecision() - 70) * 0.30103)), 6, 18);
        const QString im = shortNumber(v.cy, digits);
        const QString center = QStringLiteral("Center %1 %2 %3i").arg(shortNumber(v.cx, digits),
                                                                      im.startsWith(QLatin1Char('-')) ? QStringLiteral("−") : QStringLiteral("+"),
                                                                      im.startsWith(QLatin1Char('-')) ? im.mid(1) : im);
        QStringList parts;
        parts << center << QStringLiteral("Zoom %1×").arg(v.magnificationText())
              << QStringLiteral("%1 Iterations").arg(QLocale().toString(s.maxIterations));
        if (std::fabs(v.angle) > 1e-9) parts << QStringLiteral("Turned %1°").arg(QLocale().toString(v.angle * 180.0 / M_PI, 'f', 1));
        parts << (s.wideRange ? QStringLiteral("Extended Precision") : QStringLiteral("Standard Precision"));
        if (s.refining) parts << QStringLiteral("Refining %1 %").arg(int(s.progress * 100));
        else if (s.lastPassMs > 0) parts << QStringLiteral("Rendered in %1 ms").arg(QLocale().toString(s.lastPassMs, 'f', 0));
        m_status->setText(parts.join(QStringLiteral("  ·  ")));
        m_status->setToolTip(QStringLiteral("Copy Location (Ctrl+Shift+C) copies the center with every digit."));
        if (m_fractal.autoIterations && m_iter->value() != s.maxIterations) {
            m_syncing = true;
            m_iter->setValue(s.maxIterations);
            m_syncing = false;
        }
    }
    m_iter->setToolTip(m_fractal.autoIterations ? QStringLiteral("Auto chooses this limit. Turn Auto off to set it yourself.")
                                                : QStringLiteral("The most iterations a pixel may take before it counts as inside the set."));
    m_backBtn->setEnabled(m_historyPos > 0);
    m_forwardBtn->setEnabled(m_historyPos >= 0 && m_historyPos < int(m_history.size()) - 1);
    if (!busy && m_view->gpuTried()) m_statusTimer.stop();   // started again by the next change
    if (m_thumbsPending && m_view->gpuReady() && !busy) renderBuiltInThumbnails();
}

void MainWindow::log(const QString& text)
{
    if (!m_log) return;
    m_log->appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
}

QString MainWindow::logText() const { return m_log ? m_log->toPlainText() : QString(); }

void MainWindow::saveSettingsSoon()
{
    if (m_interactive) m_saveTimer.start();
}

void MainWindow::saveSettingsNow()
{
    if (!m_interactive) return;
    m_s.geometry = saveGeometry();
    m_s.session = currentLocation().toJson(false);
    m_s.save(appdata::settingsFile());
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    saveSettingsNow();
    if (m_interactive) savePlaces();
    QMainWindow::closeEvent(e);
}

// ------------------------------------------------------------------------------------------------ locations

core::Location MainWindow::currentLocation() const
{
    core::Location l;
    l.view = m_view->view();
    l.fractal = m_fractal;
    l.colors = m_colors;
    return l;
}

void MainWindow::applyLocation(const core::Location& l, bool remember)
{
    // A change of formula or colors this way is one step of Undo too.
    const bool changes = !l.fractal.sameFormula(m_fractal) || l.fractal.toJson() != m_fractal.toJson()
                         || (l.hasColors && !(l.colors == m_colors));
    if (changes) {
        finishEdit();   // a run of slider moves before this stays its own Undo step
        noteEdit();
    }
    m_fractal = l.fractal;
    if (l.hasColors) m_colors = l.colors;
    syncControls();
    m_view->setFractal(m_fractal);
    m_view->setColors(m_colors);
    m_view->setView(l.view);
    if (changes) finishEdit();
    if (remember) rememberView();
    saveSettingsSoon();
}

void MainWindow::openLocationFile(const QString& path)
{
    core::Location l;
    QString err;
    if (!l.load(path, &err)) {
        log(QStringLiteral("Couldn't open %1: %2").arg(QDir::toNativeSeparators(path), err));
        if (m_interactive)
            QMessageBox::warning(this, QStringLiteral("Couldn't Open Location"),
                                 QStringLiteral("Couldn't open %1.\n\n%2").arg(QDir::toNativeSeparators(path), err));
        return;
    }
    applyLocation(l);
    m_s.lastFolder = QFileInfo(path).absolutePath();
    log(QStringLiteral("Opened %1 (zoom %2×).").arg(QDir::toNativeSeparators(path), l.view.magnificationText()));
}

void MainWindow::openLocation()
{
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Open Location"), m_s.lastFolder,
                                                   QStringLiteral("Locations (*.json);;All Files (*)"), nullptr,
                                                   filedialogs::options());
    if (!f.isEmpty()) openLocationFile(f);
}

void MainWindow::saveLocation()
{
    const QString name = QStringLiteral("location-%1.json").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Save Location"), QDir(m_s.lastFolder).filePath(name),
                                                   QStringLiteral("Locations (*.json)"), nullptr, filedialogs::options());
    if (f.isEmpty()) return;
    QString err;
    if (!currentLocation().save(f, &err)) {
        log(QStringLiteral("Couldn't save %1: %2").arg(QDir::toNativeSeparators(f), err));
        QMessageBox::warning(this, QStringLiteral("Couldn't Save Location"), err);
        return;
    }
    m_s.lastFolder = QFileInfo(f).absolutePath();
    log(QStringLiteral("Saved the location to %1.").arg(QDir::toNativeSeparators(f)));
}

void MainWindow::copyLocation()
{
    QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(currentLocation().toJson()).toJson(QJsonDocument::Indented)));
    log(QStringLiteral("Copied the location (paste it with Ctrl+Shift+V, or into a text file)."));
}

void MainWindow::pasteLocation()
{
    const QByteArray text = QApplication::clipboard()->text().toUtf8();
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(text, &pe);
    core::Location l;
    QString err = QStringLiteral("The clipboard doesn't hold a location.");
    if (d.isObject() && l.fromJson(d.object(), &err)) {
        applyLocation(l);
        log(QStringLiteral("Pasted a location (zoom %1×).").arg(l.view.magnificationText()));
    } else {
        log(err);
    }
}

void MainWindow::rememberView()
{
    const core::Location cur = currentLocation();
    const QJsonObject curView = cur.view.toJson();
    if (m_historyPos >= 0 && m_historyPos < int(m_history.size())) {
        const core::Location& h = m_history[m_historyPos];
        if (h.view.toJson() == curView && h.fractal.toJson() == cur.fractal.toJson()) return;
    }
    while (int(m_history.size()) > m_historyPos + 1) m_history.removeLast();
    m_history.append(cur);
    if (m_history.size() > 300) m_history.removeFirst();
    m_historyPos = int(m_history.size()) - 1;
    updateStatus();
}

void MainWindow::back()
{
    if (m_historyPos <= 0) return;
    --m_historyPos;
    core::Location l = m_history[m_historyPos];
    l.hasColors = false;   // going back moves the view; the colors stay as they are
    applyLocation(l, false);
}

void MainWindow::forward()
{
    if (m_historyPos < 0 || m_historyPos >= int(m_history.size()) - 1) return;
    ++m_historyPos;
    core::Location l = m_history[m_historyPos];
    l.hasColors = false;
    applyLocation(l, false);
}

void MainWindow::resetView()
{
    m_view->setView(m_fractal.home());
    rememberView();
}

void MainWindow::switchFormula(core::Formula f)
{
    if (f == m_fractal.formula) return;
    finishEdit();
    noteEdit();
    m_fractal.formula = f;
    if (f == core::Formula::BurningShip) m_fractal.power = 2;
    syncControls();
    m_view->setFractal(m_fractal);
    finishEdit();
    if (!m_fractal.julia()) {
        m_view->setView(m_fractal.home());
        rememberView();
    }
    log(QStringLiteral("Now %1.").arg(m_fractal.name()));
    saveSettingsSoon();
}

void MainWindow::switchToJulia(const QString& re, const QString& im)
{
    if (!m_fractal.julia()) m_lastMandelbrotView = m_view->view();
    finishEdit();
    noteEdit();
    m_fractal.type = core::FractalType::Julia;
    m_fractal.juliaRe = re;
    m_fractal.juliaIm = im;
    syncControls();
    m_view->setFractal(m_fractal);
    finishEdit();
    m_view->setView(core::View::home(true));
    rememberView();
    log(QStringLiteral("Julia set for c = %1 %2 %3i.").arg(re, im.startsWith(QLatin1Char('-')) ? QStringLiteral("−") : QStringLiteral("+"),
                                                            im.startsWith(QLatin1Char('-')) ? im.mid(1) : im));
    saveSettingsSoon();
}

void MainWindow::switchToMandelbrot()
{
    finishEdit();
    noteEdit();
    m_fractal.type = core::FractalType::Mandelbrot;
    syncControls();
    m_view->setFractal(m_fractal);
    finishEdit();
    m_view->setView(m_lastMandelbrotView);
    rememberView();
    saveSettingsSoon();
}

void MainWindow::showViewMenu(const QPoint& global, const QString& re, const QString& im)
{
    const QPointF p = m_view->mapFromGlobal(global);
    const double hh = std::max(1, m_view->height()) / 2.0;
    const double u = (p.x() - m_view->width() / 2.0) / hh, v = -(p.y() - hh) / hh;
    QMenu menu(this);
    menu.addAction(QStringLiteral("Zoom In Here"), this, [this, u, v] { m_view->zoomAnimated(4.0, u, v); });
    menu.addAction(QStringLiteral("Center Here"), this, [this, u, v] { m_view->panAnimated(u, v); });
    menu.addSeparator();
    if (!m_fractal.julia()) {
        menu.addAction(QStringLiteral("Julia Set Here"), this, [this, re, im] { switchToJulia(re, im); });
    } else {
        menu.addAction(QStringLiteral("Back to the Mandelbrot Set"), this, [this] {
            m_syncing = true;
            m_type->setCurrentIndex(0);
            m_syncing = false;
            switchToMandelbrot();
        });
    }
    menu.addAction(QStringLiteral("Copy Coordinates"), this, [this, re, im] {
        QApplication::clipboard()->setText(re + QStringLiteral(" ") + im);
        log(QStringLiteral("Copied the point %1, %2.").arg(re.left(40), im.left(40)));
    });
    menu.addAction(QStringLiteral("Add to Places…"), this, &MainWindow::addPlace);
    menu.exec(global);
}

// ------------------------------------------------------------------------------------------------ pictures

void MainWindow::savePicture()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("Save Picture"));
    auto* form = new QFormLayout(&d);
    auto* sizes = new QComboBox;
    const QSize win = m_view->size() * m_view->devicePixelRatioF();
    const QList<QPair<QString, QSize>> presets = {
        {QStringLiteral("Window Size (%1 × %2)").arg(win.width()).arg(win.height()), win},
        {QStringLiteral("Full HD (1920 × 1080)"), QSize(1920, 1080)},
        {QStringLiteral("QHD (2560 × 1440)"), QSize(2560, 1440)},
        {QStringLiteral("4K (3840 × 2160)"), QSize(3840, 2160)},
        {QStringLiteral("8K (7680 × 4320)"), QSize(7680, 4320)},
        {QStringLiteral("Square (2000 × 2000)"), QSize(2000, 2000)},
        {QStringLiteral("Custom"), QSize()},
    };
    for (const auto& p : presets) sizes->addItem(p.first);
    sizes->setCurrentIndex(3);
    auto* wBox = new QSpinBox;
    auto* hBox = new QSpinBox;
    for (QSpinBox* b : {wBox, hBox}) {
        b->setRange(16, 32768);
        b->setSuffix(QStringLiteral(" px"));
        b->setGroupSeparatorShown(true);
    }
    wBox->setValue(3840);
    hBox->setValue(2160);
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(wBox);
    sizeRow->addWidget(new QLabel(QStringLiteral("×")));
    sizeRow->addWidget(hBox);
    auto* aa = new QComboBox;
    aa->addItems({QStringLiteral("Off"), QStringLiteral("2 × 2"), QStringLiteral("3 × 3"), QStringLiteral("4 × 4")});
    aa->setCurrentIndex(2);
    aa->setToolTip(QStringLiteral("Samples per pixel: smoother edges, and 4, 9 or 16 times the work."));
    form->addRow(QStringLiteral("Size"), sizes);
    form->addRow(QStringLiteral("Width and Height"), sizeRow);
    form->addRow(QStringLiteral("Anti-Aliasing"), aa);
    auto* note = new QLabel(QStringLiteral("Large pictures are drawn in pieces, so any size fits in memory."));
    note->setObjectName(QStringLiteral("muted"));
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Save…"));
    form->addRow(buttons);
    auto applyPreset = [&](int i) {
        const bool custom = presets[i].second.isEmpty();
        wBox->setEnabled(custom);
        hBox->setEnabled(custom);
        if (!custom) {
            wBox->setValue(presets[i].second.width());
            hBox->setValue(presets[i].second.height());
        }
    };
    applyPreset(sizes->currentIndex());
    connect(sizes, &QComboBox::currentIndexChanged, &d, applyPreset);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    const int w = wBox->value(), h = hBox->value(), ss = aa->currentIndex() + 1;

    const QString name = QStringLiteral("mandelbrot-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Save Picture"), QDir(m_s.lastFolder).filePath(name),
                                                   QStringLiteral("PNG Pictures (*.png);;JPEG Pictures (*.jpg)"), nullptr,
                                                   filedialogs::options());
    if (f.isEmpty()) return;
    QProgressDialog progress(QStringLiteral("Rendering %1 × %2…").arg(w).arg(h), QStringLiteral("Cancel"), 0, 1000, this);
    progress.setWindowTitle(QStringLiteral("Save Picture"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    QString err;
    const bool ok = savePictureTo(f, w, h, ss, &err, [&](double p) {
        progress.setValue(int(p * 1000));
        QApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.close();
    if (!ok && err != QStringLiteral("Cancelled."))
        QMessageBox::warning(this, QStringLiteral("Couldn't Save Picture"), err);
}

bool MainWindow::savePictureTo(const QString& f, int w, int h, int ss, QString* error, const std::function<bool(double)>& progress)
{
    QElapsedTimer t;
    t.start();
    QString err;
    const QImage img = m_view->renderPicture(m_view->view(), w, h, ss, &err, progress);
    if (img.isNull()) {
        log(QStringLiteral("The picture wasn't saved: %1").arg(err));
        if (error) *error = err;
        return false;
    }
    if (!img.save(f)) {
        err = QStringLiteral("Couldn't write %1.").arg(QDir::toNativeSeparators(f));
        log(err);
        if (error) *error = err;
        return false;
    }
    m_s.lastFolder = QFileInfo(f).absolutePath();
    log(QStringLiteral("Saved %1 (%2 × %3, %4 s).").arg(QDir::toNativeSeparators(f)).arg(w).arg(h)
            .arg(QLocale().toString(t.elapsed() / 1000.0, 'f', 1)));
    return true;
}

void MainWindow::setAutopilot(bool on)
{
    m_view->setAutopilot(on);
    if (on && !m_view->autopilot() && !m_view->gpuReady()) log(QStringLiteral("The autopilot needs the graphics card renderer."));
}

void MainWindow::recordZoom()
{
    setAutopilot(false);
    const bool canLoop = !m_fractal.julia() && m_fractal.deepCapable();
    const QString why = m_fractal.julia()
        ? QStringLiteral("Loops need the Mandelbrot set: Julia sets have no mini copies of themselves.")
        : !m_fractal.classic() ? QStringLiteral("Loops need the classic formula: the search for a mini copy is written for it.")
                               : QStringLiteral("Loops need power 2: the deep zoom into a mini copy only works there.");
    RecordDialog d(m_view->view(), canLoop, why, m_view->size() * m_view->devicePixelRatioF(), this);
    if (d.exec() != QDialog::Accepted) return;
    RecordSettings s = d.settings();
    if (!s.loop) s.from = m_fractal.home();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString path;
    if (s.format == MovieWriter::Format::PngFrames) {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a Folder for the Frames"), m_s.lastFolder,
                                                              filedialogs::options());
        if (dir.isEmpty()) return;
        path = QDir(dir).filePath(QStringLiteral("zoom-%1").arg(stamp));
        m_s.lastFolder = dir;
    } else {
        const QString ext = MovieWriter::extension(s.format);
        const QString filter = s.format == MovieWriter::Format::Mp4   ? QStringLiteral("MP4 Videos (*.mp4)")
                               : s.format == MovieWriter::Format::Gif ? QStringLiteral("Animated GIF (*.gif)")
                                                                      : QStringLiteral("Animated WebP (*.webp)");
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Zoom Movie"),
                                            QDir(m_s.lastFolder).filePath(QStringLiteral("zoom-%1%2").arg(stamp, ext)), filter,
                                            nullptr, filedialogs::options());
        if (path.isEmpty()) return;
        if (!path.endsWith(ext, Qt::CaseInsensitive)) path += ext;
        m_s.lastFolder = QFileInfo(path).absolutePath();
    }
    QProgressDialog progress(QStringLiteral("Rendering the zoom…"), QStringLiteral("Cancel"), 0, 1000, this);
    progress.setWindowTitle(QStringLiteral("Record Zoom"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QElapsedTimer t;
    t.start();
    QString err;
    const bool ok = recordZoomTo(s, path, &err, [&](int done, int total) {
        progress.setValue(int(1000.0 * done / std::max(total, 1)));
        if (done > 0) {
            const double left = t.elapsed() / 1000.0 * (total - done) / done;
            progress.setLabelText(QStringLiteral("Frame %1 of %2 · about %3 left")
                                      .arg(QLocale().toString(done)).arg(QLocale().toString(total))
                                      .arg(left < 90 ? QStringLiteral("%1 s").arg(int(std::ceil(left)))
                                                     : QStringLiteral("%1 min").arg(int(std::ceil(left / 60)))));
        }
        QApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.close();
    if (!ok && err != QStringLiteral("Cancelled."))
        QMessageBox::warning(this, QStringLiteral("Couldn't Record the Zoom"), err);
}

bool MainWindow::recordZoomTo(const RecordSettings& s, const QString& path, QString* error,
                              const std::function<bool(int, int)>& progress)
{
    QString err;
    if (s.loop && !s.minibrot.found) {
        err = QStringLiteral("No mini Mandelbrot was found for the loop.");
        if (error) *error = err;
        return false;
    }
    std::unique_ptr<MovieWriter> writer = MovieWriter::create(s.format, path, s.size.width(), s.size.height(), s.fps, &err);
    if (!writer) {
        log(QStringLiteral("The movie couldn't start: %1").arg(err));
        if (error) *error = err;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    const int n = s.frames();
    // A loop renders a few frames past its end and blends them into its first ones, so the wrap can't be seen.
    const int k = s.loop ? std::clamp(int(std::lround(s.crossfadeSeconds * s.fps)), 1, std::max(1, n / 3)) : 0;
    const int total = n + k;
    auto viewAt = [&](int i) {
        const double tt = s.loop ? double(i) / n : (n > 1 ? double(i) / (n - 1) : 1.0);
        return core::zoomPathAt(s.from, s.to, tt);
    };
    const core::View deepest = viewAt(n - 1 + k);
    if (!m_view->beginMovie(deepest, s.size.width(), s.size.height())) {
        m_view->endMovie();
        err = m_view->gpuReady() ? QStringLiteral("The reference orbit couldn't be computed.") : m_view->gpuError();
        if (error) *error = err;
        return false;
    }
    // A loop's colors: near a minibrot of period p the escape times run about p times higher (plus an offset), so
    // iteration-based colors would come out different at the end. Measure the offset on two small probes, and move
    // the color density and offset gradually so the last frame's colors land on the first frame's.
    double loopP = 1, loopB = 0;
    const bool usesMu = m_colors.style == int(core::ColorStyle::Gradient) || m_colors.style == int(core::ColorStyle::SineWaves);
    if (s.loop && usesMu) {
        loopP = s.minibrot.period;
        const int pw = 160, ph = std::max(16, 160 * s.size.height() / std::max(1, s.size.width()));
        const std::vector<float> a = m_view->probeEscapeTimes(s.from, pw, ph), b = m_view->probeEscapeTimes(s.to, pw, ph);
        std::vector<double> diffs;
        for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            if (a[i] >= 0 && b[i] >= 0) diffs.push_back(double(b[i]) - loopP * double(a[i]));
        if (!diffs.empty()) {
            std::nth_element(diffs.begin(), diffs.begin() + ptrdiff_t(diffs.size() / 2), diffs.end());
            loopB = diffs[diffs.size() / 2];
        }
    }
    auto colorsAt = [&](double tt) {
        core::ColorParams c = m_colors;
        if (!s.loop || !usesMu || loopP <= 1) return c;
        if (c.colorScale == 0) {           // linear: mu * D / 24 + O
            c.density = m_colors.density * std::pow(loopP, -tt);
            c.offset = m_colors.offset - tt * loopB * m_colors.density / (24.0 * loopP);
        } else if (c.colorScale == 1) {    // square root
            c.density = m_colors.density * std::pow(loopP, -0.5 * tt);
        } else {                           // logarithmic: a shift
            c.offset = m_colors.offset - tt * 0.5 * m_colors.density * std::log2(loopP);
        }
        c.offset -= std::floor(c.offset);
        return c;
    };
    int done = 0;
    bool cancelled = false;
    auto frame = [&](int i, QImage* out) {
        QString e;
        m_view->setColors(colorsAt(s.loop ? double(i) / n : 0.0));
        *out = m_view->renderPicture(viewAt(i), s.size.width(), s.size.height(), s.samplesPerSide, &e,
                                     [&](double) { return !progress || progress(done, total); }, double(i) / s.fps);
        if (out->isNull()) {
            if (e == QStringLiteral("Cancelled.")) cancelled = true;
            err = e;
            return false;
        }
        ++done;
        if (progress && !progress(done, total)) {
            cancelled = true;
            return false;
        }
        return true;
    };
    bool ok = true;
    QList<QImage> tail;
    for (int i = 0; i < k && ok; ++i) {
        QImage img;
        ok = frame(n + i, &img);
        tail << img;
    }
    for (int i = 0; i < n && ok; ++i) {
        QImage img;
        ok = frame(i, &img);
        if (!ok) break;
        if (i < k) {
            // Fade from the copy one loop deeper into the start.
            QImage mixed = tail[i].convertToFormat(QImage::Format_RGB32);
            QPainter p(&mixed);
            p.setOpacity(double(i) / k);
            p.drawImage(0, 0, img);
            p.end();
            img = mixed;
        }
        ok = writer->add(img, &err);
    }
    m_view->setColors(m_colors);
    m_view->endMovie();
    if (ok) ok = writer->finish(&err);
    writer.reset();
    if (!ok) {
        if (cancelled) err = QStringLiteral("Cancelled.");
        log(cancelled ? QStringLiteral("The zoom movie was cancelled.") : QStringLiteral("The zoom movie failed: %1").arg(err));
        // A half-written file is no use: remove it (a frames folder keeps the frames already written).
        if (s.format != MovieWriter::Format::PngFrames) QFile::remove(path);
        if (error) *error = err;
        return false;
    }
    log(QStringLiteral("Saved a %1-frame %2 to %3 (%4 × %5, %6 s to render).")
            .arg(QLocale().toString(n))
            .arg(s.loop ? QStringLiteral("seamless loop") : QStringLiteral("zoom"))
            .arg(QDir::toNativeSeparators(path))
            .arg(s.size.width()).arg(s.size.height())
            .arg(QLocale().toString(timer.elapsed() / 1000.0, 'f', 1)));
    return true;
}

void MainWindow::randomizeColors()
{
    finishEdit();   // a run of slider moves before this stays its own Undo step
    const core::ColorParams c = core::randomColorsFor(*QRandomGenerator::global(), m_colors, m_fractal, m_view->view());
    setColors(c);
    finishEdit();
    log(QStringLiteral("New random colors: %1, %2 palette.").arg(core::styleNames().value(c.style), c.palette));
}

void MainWindow::randomizeEverything()
{
    setAutopilot(false);
    const core::Scene s = core::randomScene(*QRandomGenerator::global());
    core::Location l;
    l.fractal = s.fractal;
    l.view = s.view;
    l.colors = s.colors;
    l.hasColors = true;
    if (s.fractal.julia() && !m_fractal.julia()) m_lastMandelbrotView = m_view->view();
    applyLocation(l);
    log(QStringLiteral("Random: %1 at zoom %2×, %3 with the %4 palette. Undo and Back return to the ones before.")
            .arg(s.fractal.name(), s.view.magnificationText(), core::styleNames().value(s.colors.style), s.colors.palette));
}

// ------------------------------------------------------------------------------------------------ places

QStringList MainWindow::placeNames() const
{
    QStringList out;
    for (const core::Location& l : m_placeList) out << l.name;
    return out;
}

void MainWindow::loadPlaces()
{
    m_placeList = core::builtInPlaces();
    QFile f(placesFile());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray a = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("places")).toArray();
    int bad = 0;
    for (const QJsonValue& v : a) {
        core::Location l;
        QString err;
        if (l.fromJson(v.toObject(), &err)) m_placeList << l;
        else ++bad;
    }
    if (bad) log(QStringLiteral("%1 saved %2 couldn't be read and %3 skipped.").arg(bad)
                     .arg(bad == 1 ? QStringLiteral("place") : QStringLiteral("places"))
                     .arg(bad == 1 ? QStringLiteral("was") : QStringLiteral("were")));
}

void MainWindow::savePlaces()
{
    if (!m_interactive) return;
    QJsonArray a;
    for (const core::Location& l : m_placeList)
        if (!l.builtIn) a.append(l.toJson(true));
    QJsonObject o;
    o.insert(QStringLiteral("places"), a);
    QSaveFile f(placesFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void MainWindow::refreshPlaces()
{
    const int keep = m_places->currentRow();
    m_places->clear();
    for (const core::Location& l : m_placeList) {
        auto* it = new QListWidgetItem(l.name);
        if (!l.thumbnail.isNull()) it->setIcon(QIcon(QPixmap::fromImage(l.thumbnail)));
        else {
            QPixmap blank(80, 50);
            blank.fill(QColor(0x20, 0x22, 0x28));
            it->setIcon(QIcon(blank));
        }
        it->setToolTip(QStringLiteral("%1\nZoom %2×%3").arg(l.name, l.view.magnificationText(),
                                                            l.builtIn ? QStringLiteral("\nBuilt in") : QString()));
        m_places->addItem(it);
    }
    if (keep >= 0 && keep < m_places->count()) m_places->setCurrentRow(keep);
    updatePlaceButtons();
}

void MainWindow::updatePlaceButtons()
{
    const int row = m_places->currentRow();
    const bool any = row >= 0 && row < m_placeList.size();
    const bool mine = any && !m_placeList[row].builtIn;
    m_goPlace->setEnabled(any);
    m_renamePlace->setEnabled(mine);
    m_removePlace->setEnabled(mine);
    const QString why = any && !mine ? QStringLiteral("Built-in places can't be changed.") : QStringLiteral("Choose a place first.");
    m_renamePlace->setToolTip(mine ? QStringLiteral("Gives the place a new name.") : why);
    m_removePlace->setToolTip(mine ? QStringLiteral("Removes the place from the list.") : why);
    m_goPlace->setToolTip(any ? QStringLiteral("Goes to the place (or double-click it).") : QStringLiteral("Choose a place first."));
}

void MainWindow::addPlace()
{
    QString name = QStringLiteral("Place %1").arg(m_placeList.size() - core::builtInPlaces().size() + 1);
    bool ok = false;
    name = QInputDialog::getText(this, QStringLiteral("Add to Places"), QStringLiteral("Name"), QLineEdit::Normal, name, &ok).trimmed();
    if (ok && !name.isEmpty()) addPlaceNamed(name);
}

void MainWindow::addPlaceNamed(const QString& name)
{
    if (name.trimmed().isEmpty()) return;
    core::Location l = currentLocation();
    l.name = name.trimmed().left(200);
    const QImage pic = m_view->currentPicture();
    if (!pic.isNull()) {
        // A thumbnail cut to the list's shape from the middle of the picture.
        const double want = 80.0 / 50.0;
        QRect r = pic.rect();
        if (double(r.width()) / r.height() > want) r.setWidth(int(r.height() * want));
        else r.setHeight(int(r.width() / want));
        r.moveCenter(pic.rect().center());
        l.thumbnail = pic.copy(r).scaled(160, 100, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    m_placeList << l;
    refreshPlaces();
    m_places->setCurrentRow(int(m_placeList.size()) - 1);
    m_places->scrollToBottom();
    setCardOpen(QStringLiteral("PLACES"), true);
    savePlaces();
    log(QStringLiteral("Added \"%1\" to Places.").arg(l.name));
}

void MainWindow::renamePlace()
{
    const int row = m_places->currentRow();
    if (row < 0 || row >= m_placeList.size() || m_placeList[row].builtIn) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename Place"), QStringLiteral("Name"), QLineEdit::Normal,
                                               m_placeList[row].name, &ok).trimmed();
    if (ok) renamePlaceTo(row, name);
}

void MainWindow::renamePlaceTo(int row, const QString& name)
{
    if (row < 0 || row >= m_placeList.size() || m_placeList[row].builtIn || name.trimmed().isEmpty()) return;
    m_placeList[row].name = name.trimmed().left(200);
    refreshPlaces();
    savePlaces();
}

void MainWindow::selectPlace(int row) { m_places->setCurrentRow(row); }

bool MainWindow::placeButtonsEnabled(bool* go, bool* rename, bool* remove) const
{
    *go = m_goPlace->isEnabled();
    *rename = m_renamePlace->isEnabled();
    *remove = m_removePlace->isEnabled();
    return true;
}

void MainWindow::removePlace()
{
    const int row = m_places->currentRow();
    if (row < 0 || row >= m_placeList.size() || m_placeList[row].builtIn) return;
    const QString name = m_placeList[row].name;
    m_placeList.removeAt(row);
    refreshPlaces();
    savePlaces();
    log(QStringLiteral("Removed \"%1\" from Places.").arg(name));
}

void MainWindow::goToPlace(int row)
{
    if (row < 0 || row >= m_placeList.size()) return;
    const core::Location& l = m_placeList[row];
    if (l.fractal.julia() && !m_fractal.julia()) m_lastMandelbrotView = m_view->view();
    applyLocation(l);
    log(QStringLiteral("Went to \"%1\" (zoom %2×).").arg(l.name, l.view.magnificationText()));
}

void MainWindow::renderBuiltInThumbnails()
{
    // One thumbnail per idle moment, so the window never waits for them.
    if (!m_view->gpuReady()) return;
    for (core::Location& l : m_placeList) {
        if (!l.builtIn || !l.thumbnail.isNull()) continue;
        // Thumbnails use the colors in use, at the list's shape, on the preview's own renderer (the main view keeps
        // its reference orbit).
        const QImage img = m_view->renderThumbnail(l.fractal, l.hasColors ? l.colors : m_colors, l.view, 160, 100);
        if (!img.isNull()) l.thumbnail = img;
        else l.thumbnail = QImage(160, 100, QImage::Format_RGB32);
        refreshPlaces();
        QTimer::singleShot(60, this, [this] { renderBuiltInThumbnails(); });
        return;
    }
    m_thumbsPending = false;
}
