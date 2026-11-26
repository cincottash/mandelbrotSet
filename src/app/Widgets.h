// Small widgets the cards are built from.
#pragma once

#include <QColor>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QSlider>
#include <QToolButton>
#include <QWidget>

class QGridLayout;

// A labeled slider with its number in a box beside it. The slider moves the value live; typing a number works
// too. Right-click either part for "Reset to Default".
class SliderRow : public QObject {
    Q_OBJECT
public:
    SliderRow(const QString& label, double min, double max, double def, int decimals, const QString& unit,
              bool logScale, const QString& tip, QObject* parent);
    void addTo(QGridLayout* grid, int row);
    double value() const { return m_value; }
    void setValue(double v);            // no signal
    void setVisible(bool on);
    void setEnabled(bool on);
    void setTestId(const QString& id);
    QLabel* label() const { return m_label; }
    QSlider* slider() const { return m_slider; }
    QDoubleSpinBox* box() const { return m_box; }
    double defaultValue() const { return m_def; }

signals:
    void valueChanged(double v);

private:
    int toSlider(double v) const;
    double fromSlider(int pos) const;
    void showMenu(const QPoint& global);

    QLabel* m_label;
    QSlider* m_slider;
    QDoubleSpinBox* m_box;
    double m_min, m_max, m_def, m_value;
    bool m_log;
    bool m_updating = false;
};

// A button showing a color; clicking it opens a color chooser.
class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(const QString& title, QWidget* parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor& c);     // no signal

signals:
    void colorChanged(const QColor& c);

private:
    QColor m_color;
    QString m_title;
};

// A small map of the Mandelbrot set: dragging the dot chooses the Julia set's seed.
class SeedPad : public QWidget {
    Q_OBJECT
public:
    explicit SeedPad(QWidget* parent = nullptr);
    void setSeed(double re, double im);   // no signal
    QSize sizeHint() const override { return {150, 120}; }
    QSize minimumSizeHint() const override { return {150, 120}; }
    QPointF seed() const { return m_seed; }

signals:
    void seedChanged(double re, double im);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    QPointF toPlane(const QPointF& p) const;
    QPointF toWidget(const QPointF& c) const;
    void pick(const QPointF& p);
    QImage m_map;
    QPointF m_seed{-0.8, 0.156};
};

// A strip of the palette's colors, for drop-down lists.
QIcon paletteIcon(const QString& name, int w = 64, int h = 14);

// Shared sizes, so every card's columns line up: the number boxes, and the label column.
int numberBoxWidth(const QFontMetrics& fm);
int labelColumnWidth(const QFontMetrics& fm);
