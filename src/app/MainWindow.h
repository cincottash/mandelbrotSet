// The main window: option cards on both sides of the picture (or all on the left), menus, a header and a bottom bar.
#pragma once

#include "RecordDialog.h"
#include "Settings.h"

#include "core/ColorParams.h"
#include "core/Fractal.h"
#include "core/Location.h"
#include "core/Palette.h"

#include <QHash>
#include <QList>

#include <functional>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>

class FractalView;
class SliderRow;
class ColorButton;
class SeedPad;
class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // interactive = false: screenshot and test mode (no dialogs of its own on errors, nothing saved).
    explicit MainWindow(Settings& s, bool interactive = true, QWidget* parent = nullptr);
    ~MainWindow() override;

    FractalView* view() const { return m_view; }
    void openLocationFile(const QString& path);
    void applyLocation(const core::Location& l, bool remember = true);
    core::Location currentLocation() const;
    const core::FractalParams& fractal() const { return m_fractal; }
    const core::ColorParams& colors() const { return m_colors; }
    void setColors(const core::ColorParams& c);   // as if the controls were moved
    void trigger(const QString& menuText);         // runs a menu command by its text
    QString logText() const;
    QStringList placeNames() const;
    int historySize() const { return int(m_history.size()); }
    int historyPos() const { return m_historyPos; }
    void setCardOpen(const QString& name, bool open, bool save = true);
    bool cardOpen(const QString& name) const;
    SliderRow* sliderRow(const QString& key) const { return m_rows.value(key); }
    QString statusText() const;
    void refreshStatus() { updateStatus(); }   // the header and status line, now (screenshots and tests)
    // What the dialogs do once answered (tests call these directly; they never open a window).
    bool savePictureTo(const QString& path, int w, int h, int samplesPerSide, QString* error,
                       const std::function<bool(double)>& progress = {});
    void addPlaceNamed(const QString& name);
    void renamePlaceTo(int row, const QString& name);
    void selectPlace(int row);
    // Record Zoom once its dialog is answered: renders every frame and writes the movie. `progress` gets the frames
    // done and the total, and returns false to cancel.
    bool recordZoomTo(const RecordSettings& s, const QString& path, QString* error,
                      const std::function<bool(int, int)>& progress = {});
    void setAutopilot(bool on);
    void setAnimationPaused(bool on);
    // Settings history (colors and formula): every change can be undone and redone.
    void undo();
    void redo();
    int undoSteps() const { return int(m_undo.size()); }
    int redoSteps() const { return int(m_redo.size()); }
    void finishEdit();                 // ends the current run of slider moves now (tests)
    void resetCard(const QString& title);
    void randomizeEverything();
    void randomizeColors();
    // Edit Palette, in steps (the dialog drives these; tests call them directly): the picture follows the palette
    // being edited; finishing with save keeps it among the user's palettes, else everything goes back.
    void beginPaletteEdit();
    void previewPalette(const core::Palette& p);
    void finishPaletteEdit(bool save, const core::Palette& p);
    void deleteCustomPalette(const QString& name);
    QStringList paletteChoices() const;   // the palette list's entries, in order
    // The Gallery: whole pictures (spot, fractal, colors and effects). The user's own come first, newest first,
    // then the famous fractals in their own looks (the card, and Explore > Gallery).
    void showGalleryItem(int i);
    int galleryCount() const { return int(m_gallery.size()); }
    QImage galleryThumbnail(int i) const { return i >= 0 && i < m_gallery.size() ? m_gallery[i].thumbnail : QImage(); }
    QString galleryName(int i) const { return i >= 0 && i < m_gallery.size() ? m_gallery[i].name : QString(); }
    bool galleryItemIsMine(int i) const { return i >= 0 && i < m_gallery.size() && !m_gallery[i].builtIn; }
    int myGalleryCount() const;
    void saveToGalleryNamed(const QString& name);
    void renameGalleryItem(int i, const QString& name);
    void removeGalleryItem(int i);
    bool placeButtonsEnabled(bool* go, bool* rename, bool* remove) const;
    // The cards in two columns (left and right of the picture) or all on the left.
    void setCardsBothSides(bool on);
    bool cardOnRight(const QString& name) const;
    // File > Settings > Language: kept at once; offers to restart in it (the window closes, main() starts the app again).
    void chooseLanguage(const QString& code);
    struct Restart { QString program; QStringList args; };
    static Restart& restartAfterQuit();                 // empty unless a restart was asked for
    // Tests: the answer to "Restart now?" and the restart itself, without a window or a new process.
    static inline std::function<bool()> restartAnswer;
    static inline std::function<bool(const QString&, const QStringList&)> startAgain;
    // A palette's name as shown: the built-in ones in the app's language, the user's own as they named them.
    static QString paletteShownName(const QString& name);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    struct Card { QFrame* frame; QWidget* body; QToolButton* toggle; };

    void buildMenus();
    void buildUi();
    QFrame* makeCard(const QString& title, QVBoxLayout** body);
    QToolButton* linkButton(const QString& text);
    void updateStretch();
    QWidget* buildFractalCard();
    QWidget* buildColorCard();
    QGridLayout* sliderGrid(const char* card, QVBoxLayout* into);
    void buildPlaces(QVBoxLayout* body);

    void syncControls();
    void updateStyleVisibility();
    void fractalEdited();
    void colorsEdited();
    void updateStatus();
    void log(const QString& text);

    void applyTheme();
    void previewTheme(const QString& key);
    void endThemePreview();
    QAction* addThemeAction(QMenu* menu, const QString& text, const QString& key);
    void applyOpacity();
    bool followsOpacity(const QWidget* w) const;
    void saveSettingsSoon();
    void saveSettingsNow();

    void savePicture();
    void recordZoom();
    void saveLocation();
    void openLocation();
    void copyLocation();
    void pasteLocation();
    void rememberView();
    void back();
    void forward();
    void resetView();
    struct SettingsState {
        core::FractalParams fractal;
        core::ColorParams colors;
    };
    SettingsState currentSettings() const { return {m_fractal, m_colors}; }
    void noteEdit();                   // a change is made: remember the state before it (once per run of moves)
    void restoreSettings(const SettingsState& s);
    void updateAnimationButtons();
    void showViewMenu(const QPoint& global, const QString& re, const QString& im);
    void editPalette();
    void refreshPaletteList();
    void switchToJulia(const QString& re, const QString& im);
    void switchToMandelbrot();
    void switchFormula(core::Formula f);
    void switchToBuddhabrot();

    void loadPlaces();
    void savePlaces();
    void refreshPlaces();
    void addPlace();
    void renamePlace();
    void removePlace();
    void goToPlace(int row);
    void updatePlaceButtons();
    void renderBuiltInThumbnails();
    void loadGallery();
    void saveGallery();
    void refreshGallery();
    void saveToGallery();
    void rebuildGalleryMenu();
    void arrangeCards();

    Settings& m_s;
    bool m_interactive;
    core::FractalParams m_fractal;
    core::ColorParams m_colors;
    core::View m_lastMandelbrotView;
    bool m_syncing = false;

    FractalView* m_view = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_pill = nullptr;
    QToolButton* m_notKept = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_gpuMessage = nullptr;
    QVBoxLayout* m_leftCol = nullptr;
    QVBoxLayout* m_rightCol = nullptr;
    QWidget* m_rightScroll = nullptr;
    QStringList m_cardOrder;            // every card, in its one-column order
    QHash<QString, Card> m_cards;
    QHash<QString, SliderRow*> m_rows;

    // FRACTAL
    QComboBox* m_type = nullptr;
    QComboBox* m_formula = nullptr;
    // BUDDHABROT
    QComboBox* m_buddhaColoring = nullptr;
    SliderRow* m_buddhaRed = nullptr;
    SliderRow* m_buddhaGreen = nullptr;
    SliderRow* m_buddhaBlue = nullptr;
    SliderRow* m_buddhaMin = nullptr;
    SliderRow* m_power = nullptr;
    SliderRow* m_escape = nullptr;
    QCheckBox* m_autoIter = nullptr;
    QSpinBox* m_iter = nullptr;
    QWidget* m_seedBox = nullptr;
    SeedPad* m_seedPad = nullptr;
    QLineEdit* m_seedRe = nullptr;
    QLineEdit* m_seedIm = nullptr;
    QLabel* m_juliaHint = nullptr;
    // COLOR
    QComboBox* m_look = nullptr;
    QComboBox* m_style = nullptr;
    QComboBox* m_palette = nullptr;
    QToolButton* m_paletteEdit = nullptr;
    // Edit Palette in progress: what to go back to on Cancel.
    QList<core::Palette> m_palettesBeforeEdit;
    core::ColorParams m_colorsBeforeEdit;
    bool m_editingPalette = false;
    QComboBox* m_scale = nullptr;
    QLabel* m_paletteLabel = nullptr;
    QLabel* m_scaleLabel = nullptr;
    ColorButton* m_moireTint = nullptr;
    QLabel* m_moireTintLabel = nullptr;
    QComboBox* m_trapShape = nullptr;
    QLabel* m_trapShapeLabel = nullptr;
    ColorButton* m_edgeColor = nullptr;
    QComboBox* m_interior = nullptr;
    ColorButton* m_in1 = nullptr;
    ColorButton* m_in2 = nullptr;
    QLabel* m_in2Label = nullptr;
    ColorButton* m_sphereBg = nullptr;
    // PLACES
    QListWidget* m_places = nullptr;
    QPushButton* m_goPlace = nullptr;
    QPushButton* m_renamePlace = nullptr;
    QPushButton* m_removePlace = nullptr;
    QList<core::Location> m_placeList;   // built-in first, then the user's
    // GALLERY
    QListWidget* m_galleryList = nullptr;
    QList<core::Location> m_gallery;    // the user's (builtIn false) first, then the famous ones
    QMenu* m_galleryMenu = nullptr;
    QAction* m_bothSidesAct = nullptr;
    // ACTIVITY
    QPlainTextEdit* m_log = nullptr;
    // Bottom bar
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_forwardBtn = nullptr;
    QPushButton* m_autoBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QAction* m_autoAct = nullptr;
    QAction* m_pauseAct = nullptr;
    QAction* m_undoAct = nullptr;
    QAction* m_redoAct = nullptr;

    // Settings history
    QList<SettingsState> m_undo, m_redo;
    SettingsState m_lastState;          // the settings when the last run of changes ended
    QTimer m_editBurst;                 // moves of one slider within a moment are one step
    bool m_restoring = false;

    // Menus
    QMenu* m_viewMenu = nullptr;
    QActionGroup* m_themeGroup = nullptr;
    QString m_themePreviewing;
    QTimer m_themePreviewTimer;
    QAction* m_juliaPreviewAct = nullptr;
    QAction* m_onTopAct = nullptr;
    QActionGroup* m_aaGroup = nullptr;
    QAction* m_backAct = nullptr;
    QAction* m_forwardAct = nullptr;
    QHash<int, bool> m_effectsDefault;

    // History of views (Back and Forward)
    QList<core::Location> m_history;
    int m_historyPos = -1;

    QTimer m_saveTimer;
    QTimer m_statusTimer;
    QString m_lastGpuLine;
    bool m_thumbsPending = true;
};
