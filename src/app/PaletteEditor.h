// Edit Palette: the user's own color gradients. A bar shows the gradient with its color stops below it; drag a stop
// to move it, double-click the bar to add one, Delete removes the chosen one. Every change shows on the picture at
// once (the window listens to changed()); Save keeps the palette under its name, Cancel puts everything back.
#pragma once

#include "core/Palette.h"

#include <QDialog>
#include <QWidget>

class ColorButton;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

// The gradient with its stops (the editor's main part).
class GradientBar : public QWidget {
    Q_OBJECT
public:
    explicit GradientBar(QWidget* parent = nullptr);
    void setPalette(const core::Palette& p);
    const core::Palette& gradient() const { return m_p; }
    int selected() const { return m_sel; }
    void select(int i);
    QSize sizeHint() const override { return {440, 76}; }
    QSize minimumSizeHint() const override { return {240, 76}; }

signals:
    void edited();            // stops moved, added or removed
    void selectionChanged(int i);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QRectF barRect() const;
    double posAt(double x) const;
    int stopAt(const QPointF& p) const;
    core::Palette m_p;
    int m_sel = 0;
    bool m_dragging = false;
};

class PaletteEditor : public QDialog {
    Q_OBJECT
public:
    // `start` is the palette to begin from (a built-in one is copied under a new name).
    PaletteEditor(const core::Palette& start, QWidget* parent = nullptr);
    core::Palette result() const;
    QString problem() const;  // why Save is unavailable (empty when it can save)

    // What the buttons do (tests call these directly).
    void addStop();           // halfway between the chosen stop and the next
    void removeStop();
    void spreadEvenly();
    void reverse();
    void setStopColor(int i, const QColor& c);
    void setStopPosition(int i, double pos);
    void setName(const QString& name);
    GradientBar* bar() const { return m_bar; }

signals:
    void changed(const core::Palette& p);

private:
    void refresh();           // after any change: the Save button, the hint, the stop controls, changed()
    void syncStopControls();

    GradientBar* m_bar = nullptr;
    QLineEdit* m_name = nullptr;
    ColorButton* m_color = nullptr;
    QDoubleSpinBox* m_pos = nullptr;
    QPushButton* m_remove = nullptr;
    QPushButton* m_save = nullptr;
    QLabel* m_hint = nullptr;
    QString m_originalName;   // a custom palette being edited may keep its own name
    bool m_syncing = false;
};
