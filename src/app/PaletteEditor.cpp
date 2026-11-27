#include "PaletteEditor.h"

#include "Widgets.h"
#include "core/Tr.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- the bar

GradientBar::GradientBar(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setToolTip(T("Drag a stop to move it. Double-click the bar to add a stop; Delete removes the chosen one."));
}

void GradientBar::setPalette(const core::Palette& p)
{
    m_p = p;
    m_sel = std::clamp(m_sel, 0, int(m_p.stops.size()) - 1);
    update();
}

void GradientBar::select(int i)
{
    if (m_p.stops.isEmpty()) return;
    i = std::clamp(i, 0, int(m_p.stops.size()) - 1);
    if (i == m_sel) return;
    m_sel = i;
    update();
    emit selectionChanged(i);
}

QRectF GradientBar::barRect() const { return QRectF(10, 6, width() - 20, height() - 34); }

double GradientBar::posAt(double x) const
{
    const QRectF r = barRect();
    return std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
}

int GradientBar::stopAt(const QPointF& p) const
{
    const QRectF r = barRect();
    int best = -1;
    double bestD = 12;
    for (int i = 0; i < m_p.stops.size(); ++i) {
        const double x = r.left() + m_p.stops[i].first * r.width();
        const double d = std::abs(p.x() - x);
        if (p.y() >= r.bottom() - 4 && d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void GradientBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = barRect();
    // The gradient, one column at a time (the same smooth steps the picture uses).
    QImage strip(std::max(1, int(r.width())), 1, QImage::Format_RGB32);
    for (int x = 0; x < strip.width(); ++x) strip.setPixel(x, 0, m_p.at((x + 0.5) / strip.width()).rgb());
    QPainterPath clip;
    clip.addRoundedRect(r, 5, 5);
    p.save();
    p.setClipPath(clip);
    p.drawImage(r, strip);
    p.restore();
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 5, 5);
    // The stops: a pointer below the bar, filled with the stop's color; the chosen one ringed in the accent color.
    for (int i = 0; i < m_p.stops.size(); ++i) {
        const double x = r.left() + m_p.stops[i].first * r.width();
        const bool sel = i == m_sel;
        QPainterPath tri;
        const double top = r.bottom() + 2, w = sel ? 8 : 6.5, h = sel ? 18 : 15;
        tri.moveTo(x, top);
        tri.lineTo(x + w, top + h * 0.45);
        tri.lineTo(x + w, top + h);
        tri.lineTo(x - w, top + h);
        tri.lineTo(x - w, top + h * 0.45);
        tri.closeSubpath();
        p.setBrush(m_p.stops[i].second);
        p.setPen(QPen(sel ? palette().color(QPalette::Highlight) : palette().color(QPalette::WindowText), sel ? 2.2 : 1.0));
        p.drawPath(tri);
    }
}

void GradientBar::mousePressEvent(QMouseEvent* e)
{
    const int i = stopAt(e->position());
    if (i >= 0) {
        select(i);
        m_dragging = true;
    }
}

void GradientBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging && m_sel >= 0 && m_sel < m_p.stops.size()) {
        m_p.stops[m_sel].first = posAt(e->position().x());
        // Keep the list sorted, following the dragged stop.
        const QColor c = m_p.stops[m_sel].second;
        const double pos = m_p.stops[m_sel].first;
        m_p.sortStops();
        for (int k = 0; k < m_p.stops.size(); ++k)
            if (m_p.stops[k].first == pos && m_p.stops[k].second == c) m_sel = k;
        update();
        emit edited();
        emit selectionChanged(m_sel);
    }
    setCursor(stopAt(e->position()) >= 0 || m_dragging ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void GradientBar::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

void GradientBar::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (stopAt(e->position()) >= 0 || m_p.stops.size() >= 64) return;
    const double pos = posAt(e->position().x());
    m_p.stops.append({pos, m_p.at(pos)});
    m_p.sortStops();
    for (int k = 0; k < m_p.stops.size(); ++k)
        if (m_p.stops[k].first == pos) m_sel = k;
    update();
    emit edited();
    emit selectionChanged(m_sel);
}

void GradientBar::keyPressEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && m_p.stops.size() > 2) {
        m_p.stops.removeAt(m_sel);
        m_sel = std::min(m_sel, int(m_p.stops.size()) - 1);
        update();
        emit edited();
        emit selectionChanged(m_sel);
        return;
    }
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        select(m_sel + (e->key() == Qt::Key_Left ? -1 : 1));
        return;
    }
    QWidget::keyPressEvent(e);
}

// ---------------------------------------------------------------- the dialog

PaletteEditor::PaletteEditor(const core::Palette& start, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(T("Edit Palette"));
    core::Palette p = start;
    if (core::isCustomPalette(start.name)) {
        m_originalName = start.name;
    } else {
        // A built-in palette is the start of a new one of the user's own, named in the app's language.
        const QString base = Tname(start.name);
        QString name = T("%1 Custom", "palette").arg(base);
        for (int n = 2; core::hasPalette(name); ++n) name = T("%1 Custom %2", "palette").arg(base).arg(n);
        p.name = name;
    }
    p.sortStops();

    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(10);
    auto* top = new QGridLayout;
    top->addWidget(new QLabel(T("Name")), 0, 0);
    m_name = new QLineEdit(p.name);
    m_name->setMaxLength(40);
    m_name->setProperty("testId", QStringLiteral("paletteName"));
    top->addWidget(m_name, 0, 1);
    lay->addLayout(top);
    m_bar = new GradientBar;
    m_bar->setPalette(p);
    lay->addWidget(m_bar);

    auto* stopRow = new QHBoxLayout;
    stopRow->addWidget(new QLabel(T("Stop Color")));
    m_color = new ColorButton(T("Stop Color"));
    m_color->setProperty("testId", QStringLiteral("stopColor"));
    stopRow->addWidget(m_color);
    stopRow->addSpacing(12);
    stopRow->addWidget(new QLabel(T("Position")));
    m_pos = new QDoubleSpinBox;
    m_pos->setRange(0, 100);
    m_pos->setDecimals(1);
    m_pos->setSuffix(QStringLiteral(" %"));
    m_pos->setKeyboardTracking(false);
    m_pos->setProperty("testId", QStringLiteral("stopPosition"));
    stopRow->addWidget(m_pos);
    stopRow->addStretch();
    lay->addLayout(stopRow);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(T("Add Stop"));
    m_remove = new QPushButton(T("Remove Stop"));
    auto* spread = new QPushButton(T("Spread Evenly"));
    auto* rev = new QPushButton(T("Reverse"));
    add->setToolTip(T("A new stop halfway to the next one, in the color already there."));
    spread->setToolTip(T("Spaces the stops evenly, keeping their order."));
    rev->setToolTip(T("Runs the colors the other way."));
    for (QPushButton* b : {add, m_remove, spread, rev}) buttons->addWidget(b);
    buttons->addStretch();
    lay->addLayout(buttons);

    m_hint = new QLabel;
    m_hint->setObjectName(QStringLiteral("muted"));
    m_hint->setWordWrap(true);
    lay->addWidget(m_hint);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    m_save = box->button(QDialogButtonBox::Save);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(add, &QPushButton::clicked, this, &PaletteEditor::addStop);
    connect(m_remove, &QPushButton::clicked, this, &PaletteEditor::removeStop);
    connect(spread, &QPushButton::clicked, this, &PaletteEditor::spreadEvenly);
    connect(rev, &QPushButton::clicked, this, &PaletteEditor::reverse);
    connect(m_bar, &GradientBar::edited, this, &PaletteEditor::refresh);
    connect(m_bar, &GradientBar::selectionChanged, this, &PaletteEditor::syncStopControls);
    connect(m_color, &ColorButton::colorChanged, this, [this](const QColor& c) { setStopColor(m_bar->selected(), c); });
    connect(m_pos, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_syncing) setStopPosition(m_bar->selected(), v / 100.0);
    });
    connect(m_name, &QLineEdit::textChanged, this, &PaletteEditor::refresh);
    syncStopControls();
    refresh();
    resize(500, sizeHint().height());
}

core::Palette PaletteEditor::result() const
{
    core::Palette p = m_bar->gradient();
    p.name = m_name->text().trimmed();
    return p;
}

QString PaletteEditor::problem() const
{
    const QString n = m_name->text().trimmed();
    if (n.isEmpty()) return T("Give the palette a name to save it.");
    if (core::isBuiltInPalette(n)) return T("\"%1\" is a built-in palette; choose another name.").arg(n);
    return {};
}

void PaletteEditor::refresh()
{
    const QString why = problem();
    m_save->setEnabled(why.isEmpty());
    const QString n = m_name->text().trimmed();
    if (!why.isEmpty()) m_hint->setText(why);
    else if (core::isCustomPalette(n) && n.compare(m_originalName, Qt::CaseInsensitive) != 0)
        m_hint->setText(T("A palette of yours is already called \"%1\": saving replaces it.").arg(n));
    else m_hint->setText(T("Changes show on the picture as you make them. Your palettes are listed "
                           "after the built-in ones."));
    m_remove->setEnabled(m_bar->gradient().stops.size() > 2);
    syncStopControls();
    emit changed(result());
}

void PaletteEditor::syncStopControls()
{
    const core::Palette& p = m_bar->gradient();
    const int i = m_bar->selected();
    if (i < 0 || i >= p.stops.size()) return;
    m_syncing = true;
    m_color->setColor(p.stops[i].second);
    m_pos->setValue(p.stops[i].first * 100.0);
    m_syncing = false;
}

void PaletteEditor::addStop()
{
    core::Palette p = m_bar->gradient();
    if (p.stops.size() >= 64) return;
    const int i = m_bar->selected();
    const double a = p.stops[i].first;
    const double b = i + 1 < p.stops.size() ? p.stops[i + 1].first : p.stops[0].first + 1.0;
    double pos = (a + b) / 2.0;
    if (pos >= 1.0) pos -= 1.0;
    p.stops.append({pos, p.at(pos)});
    p.sortStops();
    m_bar->setPalette(p);
    for (int k = 0; k < p.stops.size(); ++k)
        if (p.stops[k].first == pos) m_bar->select(k);
    refresh();
}

void PaletteEditor::removeStop()
{
    core::Palette p = m_bar->gradient();
    if (p.stops.size() <= 2) return;
    const int i = m_bar->selected();
    p.stops.removeAt(i);
    m_bar->setPalette(p);
    m_bar->select(std::min(i, int(p.stops.size()) - 1));
    refresh();
}

void PaletteEditor::spreadEvenly()
{
    core::Palette p = m_bar->gradient();
    for (int k = 0; k < p.stops.size(); ++k) p.stops[k].first = double(k) / p.stops.size();
    m_bar->setPalette(p);
    refresh();
}

void PaletteEditor::reverse()
{
    core::Palette p = m_bar->gradient();
    for (auto& s : p.stops) s.first = s.first == 0.0 ? 0.0 : 1.0 - s.first;
    p.sortStops();
    m_bar->setPalette(p);
    refresh();
}

void PaletteEditor::setStopColor(int i, const QColor& c)
{
    core::Palette p = m_bar->gradient();
    if (i < 0 || i >= p.stops.size() || !c.isValid()) return;
    p.stops[i].second = c;
    m_bar->setPalette(p);
    refresh();
}

void PaletteEditor::setStopPosition(int i, double pos)
{
    core::Palette p = m_bar->gradient();
    if (i < 0 || i >= p.stops.size()) return;
    const QColor c = p.stops[i].second;
    pos = std::clamp(pos, 0.0, 1.0);
    p.stops[i].first = pos;
    p.sortStops();
    m_bar->setPalette(p);
    for (int k = 0; k < p.stops.size(); ++k)
        if (p.stops[k].first == pos && p.stops[k].second == c) m_bar->select(k);
    refresh();
}

void PaletteEditor::setName(const QString& name) { m_name->setText(name); }
