// Color gradients for the coloring pass: a list of color stops, sampled into a table the shader repeats.
#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace core {

struct Palette {
    QString name;
    QList<QPair<double, QColor>> stops;   // positions 0..1, sorted; the gradient wraps from the last to the first
    // `count` colors evenly across 0..1 (RGBA, 8 bits each), smooth and wrapping.
    QVector<quint32> sample(int count) const;
    QColor at(double t) const;
};

const QList<Palette>& palettes();
const Palette& paletteByName(const QString& name);   // the first palette for an unknown name
QStringList paletteNames();

}  // namespace core
