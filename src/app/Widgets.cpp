#include "Widgets.h"

#include "core/Palette.h"

#include <QColorDialog>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <complex>

// ---------------------------------------------------------------- shared sizes

int numberBoxWidth(const QFontMetrics& fm) { return fm.horizontalAdvance(QStringLiteral("-0.000 /s")) + 24; }

int labelColumnWidth(const QFontMetrics& fm)
{
    int w = 0;
    for (const char* t : {"Escape Radius", "Green Frequency", "Pattern Scale", "Second Color", "Light Height", "Iterations"})
        w = std::max(w, fm.horizontalAdvance(QString::fromLatin1(t)));
    return w + 6;
}

// ---------------------------------------------------------------- SliderRow

SliderRow::SliderRow(const QString& label, double min, double max, double def, int decimals, const QString& unit,
                     bool logScale, const QString& tip, QObject* parent)
    : QObject(parent), m_min(min), m_max(max), m_def(def), m_value(def), m_log(logScale && min > 0)
{
    m_label = new QLabel(label);
    m_label->setToolTip(tip);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 1000);
    m_slider->setToolTip(tip);
    m_slider->setMinimumWidth(90);
    m_box = new QDoubleSpinBox;
    m_box->setRange(min, max);
    m_box->setDecimals(decimals);
    m_box->setSingleStep(decimals == 0 ? 1.0 : std::pow(10.0, -decimals) * (max - min > 10 ? 10 : 1));
    if (!unit.isEmpty()) m_box->setSuffix((unit == QStringLiteral("°") ? QString() : QStringLiteral(" ")) + unit);
    m_box->setKeyboardTracking(false);
    m_box->setToolTip(tip);
    m_box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    // Every number box has the same width, so sliders and boxes line up across all the cards.
    m_box->setFixedWidth(numberBoxWidth(m_box->fontMetrics()));
    setValue(def);
    connect(m_slider, &QSlider::valueChanged, this, [this](int pos) {
        if (m_updating) return;
        m_value = fromSlider(pos);
        m_updating = true;
        m_box->setValue(m_value);
        m_updating = false;
        emit valueChanged(m_value);
    });
    connect(m_box, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_updating) return;
        m_value = v;
        m_updating = true;
        m_slider->setValue(toSlider(v));
        m_updating = false;
        emit valueChanged(m_value);
    });
    for (QWidget* w : {static_cast<QWidget*>(m_label), static_cast<QWidget*>(m_slider), static_cast<QWidget*>(m_box)}) {
        w->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(w, &QWidget::customContextMenuRequested, this, [this, w](const QPoint& p) { showMenu(w->mapToGlobal(p)); });
    }
}

void SliderRow::addTo(QGridLayout* grid, int row)
{
    grid->addWidget(m_label, row, 0);
    grid->addWidget(m_slider, row, 1);
    grid->addWidget(m_box, row, 2);
}

int SliderRow::toSlider(double v) const
{
    double t;
    if (m_log) t = std::log(std::clamp(v, m_min, m_max) / m_min) / std::log(m_max / m_min);
    else t = (std::clamp(v, m_min, m_max) - m_min) / (m_max - m_min);
    return int(std::lround(t * 1000.0));
}

double SliderRow::fromSlider(int pos) const
{
    const double t = pos / 1000.0;
    double v = m_log ? m_min * std::pow(m_max / m_min, t) : m_min + t * (m_max - m_min);
    // Round to what the box shows, so the slider never holds a value the box can't.
    const double q = std::pow(10.0, m_box->decimals());
    return std::clamp(std::round(v * q) / q, m_min, m_max);
}

void SliderRow::setValue(double v)
{
    m_value = std::clamp(v, m_min, m_max);
    m_updating = true;
    m_box->setValue(m_value);
    m_slider->setValue(toSlider(m_value));
    m_updating = false;
}

void SliderRow::setVisible(bool on)
{
    m_label->setVisible(on);
    m_slider->setVisible(on);
    m_box->setVisible(on);
}

void SliderRow::setEnabled(bool on)
{
    m_label->setEnabled(on);
    m_slider->setEnabled(on);
    m_box->setEnabled(on);
}

void SliderRow::setTestId(const QString& id)
{
    m_slider->setProperty("testId", id);
    m_box->setProperty("testId", id + QStringLiteral("Box"));
}

void SliderRow::showMenu(const QPoint& global)
{
    QMenu menu;
    QAction* reset = menu.addAction(QStringLiteral("Reset to Default (%1)").arg(m_box->textFromValue(m_def) + m_box->suffix()));
    reset->setEnabled(std::fabs(m_value - m_def) > 1e-12);
    if (menu.exec(global) == reset) {
        setValue(m_def);
        emit valueChanged(m_value);
    }
}

// ---------------------------------------------------------------- ColorButton

ColorButton::ColorButton(const QString& title, QWidget* parent) : QToolButton(parent), m_title(title)
{
    setObjectName(QStringLiteral("tool"));
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setColor(Qt::black);
    connect(this, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, window(), m_title, QColorDialog::DontUseNativeDialog);
        if (c.isValid() && c != m_color) {
            setColor(c);
            emit colorChanged(c);
        }
    });
}

void ColorButton::setColor(const QColor& c)
{
    m_color = c;
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(128, 128, 128), 1));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, 17, 17), 4, 4);
    p.end();
    setIcon(QIcon(pm));
    setText(c.name(QColor::HexRgb).toUpper());
}

// ---------------------------------------------------------------- SeedPad

namespace {
constexpr double kPadLeft = -2.1, kPadRight = 0.7, kPadHalfH = 1.25;
}

SeedPad::SeedPad(QWidget* parent) : QWidget(parent)
{
    setCursor(Qt::CrossCursor);
    setToolTip(QStringLiteral("Drag the dot over the Mandelbrot set to choose the Julia set's seed. Seeds near the "
                              "set's edge give the most intricate Julia sets."));
    setProperty("testId", QStringLiteral("seedPad"));
}

QPointF SeedPad::toPlane(const QPointF& p) const
{
    const double x = kPadLeft + (p.x() / std::max(1, width())) * (kPadRight - kPadLeft);
    const double y = kPadHalfH - (p.y() / std::max(1, height())) * 2 * kPadHalfH;
    return {x, y};
}

QPointF SeedPad::toWidget(const QPointF& c) const
{
    return {(c.x() - kPadLeft) / (kPadRight - kPadLeft) * width(), (kPadHalfH - c.y()) / (2 * kPadHalfH) * height()};
}

void SeedPad::setSeed(double re, double im)
{
    m_seed = QPointF(re, im);
    update();
}

void SeedPad::pick(const QPointF& p)
{
    const QPointF c = toPlane(QPointF(std::clamp(p.x(), 0.0, double(width())), std::clamp(p.y(), 0.0, double(height()))));
    // Four decimals: enough to aim, and the boxes beside the pad take any number of digits.
    m_seed = QPointF(std::round(c.x() * 10000.0) / 10000.0, std::round(c.y() * 10000.0) / 10000.0);
    update();
    emit seedChanged(m_seed.x(), m_seed.y());
}

void SeedPad::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) pick(e->position());
}

void SeedPad::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton) pick(e->position());
}

void SeedPad::paintEvent(QPaintEvent*)
{
    const qreal dpr = devicePixelRatioF();
    const QSize px(int(width() * dpr), int(height() * dpr));
    if (m_map.size() != px) {
        // A quick map of the set, in the theme's own colors.
        const QColor in = palette().color(QPalette::Text), out = palette().color(QPalette::Base);
        const QColor mid = palette().color(QPalette::Highlight);
        m_map = QImage(px, QImage::Format_RGB32);
        for (int y = 0; y < px.height(); ++y)
            for (int x = 0; x < px.width(); ++x) {
                const QPointF c = toPlane(QPointF((x + 0.5) / dpr, (y + 0.5) / dpr));
                std::complex<double> z = 0, cc(c.x(), c.y());
                int n = 0;
                for (; n < 80 && std::norm(z) < 16; ++n) z = z * z + cc;
                QColor col;
                if (n >= 80) col = in;
                else {
                    const double t = std::pow(n / 80.0, 0.45);
                    col = QColor::fromRgbF(float(out.redF() + (mid.redF() - out.redF()) * t),
                                           float(out.greenF() + (mid.greenF() - out.greenF()) * t),
                                           float(out.blueF() + (mid.blueF() - out.blueF()) * t));
                }
                m_map.setPixel(x, y, col.rgb());
            }
        m_map.setDevicePixelRatio(dpr);
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 6, 6);
    p.setClipPath(clip);
    p.drawImage(QPointF(0, 0), m_map);
    const QPointF d = toWidget(m_seed);
    p.setPen(QPen(Qt::black, 3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(d, 5, 5);
    p.setPen(QPen(Qt::white, 1.6));
    p.drawEllipse(d, 5, 5);
    p.setClipping(false);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
}

// ---------------------------------------------------------------- palette icons

QIcon paletteIcon(const QString& name, int w, int h)
{
    const core::Palette& pal = core::paletteByName(name);
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    for (int x = 0; x < w; ++x) {
        p.setPen(pal.at(double(x) / w));
        p.drawLine(x, 1, x, h - 2);
    }
    p.setPen(QColor(128, 128, 128, 160));
    p.setBrush(Qt::NoBrush);
    p.drawRect(0, 1, w - 1, h - 3);
    p.end();
    return QIcon(pm);
}
